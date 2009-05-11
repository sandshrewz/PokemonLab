/* 
 * File:   NetworkBattle.cpp
 * Author: Catherine
 *
 * Created on April 19, 2009, 1:18 AM
 *
 * This file is a part of Shoddy Battle.
 * Copyright (C) 2009  Catherine Fitzpatrick and Benjamin Gwin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, visit the Free Software Foundation, Inc.
 * online at http://gnu.org.
 */

#include <vector>
#include <boost/thread.hpp>
#include <boost/function.hpp>
#include <boost/enable_shared_from_this.hpp>
#include "NetworkBattle.h"
#include "ThreadedQueue.h"
#include "network.h"
#include "Channel.h"
#include "../mechanics/JewelMechanics.h"
#include "../scripting/ScriptMachine.h"

using namespace std;

namespace shoddybattle { namespace network {

class NetworkBattleImpl;
class BattleChannel;

typedef boost::shared_ptr<BattleChannel> BattleChannelPtr;

/**
 * In Shoddy Battle 2, every battle is also a channel. The participants in
 * the battle are initially granted +ao. Anybody with +o or higher on the main
 * chat is granted +q in every battle.
 *
 * The initial participants of a battle join the battle directly, but all
 * spectators join the channel rather than the underlying battle. When the
 * NetworkBattle broadcasts a message, it is sent to everybody in the channel.
 * Indeed, most features related to battles are actually associated to the
 * channel.
 *
 * The name of the channel contains the participants in the battle; the topic
 * encodes the ladder, if any, on which the battle is taking place and
 * possibly other metadata.
 *
 * When the channel becomes completely empty, or if a set amount of time
 * passes without another message being posted, the channel -- and hence the
 * associated NetworkBattle -- is destroyed.
 */
class BattleChannel : public Channel {
public:

    static BattleChannelPtr createChannel(Server *server,
            NetworkBattleImpl *field) {
        BattleChannelPtr p(new BattleChannel(server, field,
                string(), string(),
                CHANNEL_FLAGS()));
        server->addChannel(p);
        return p;
    }

    Type::TYPE getChannelType() const {
        return Type::BATTLE;
    }

    void commitStatusFlags(ClientPtr client, FLAGS flags) {
        // does nothing in a BattleChannel
    }

    FLAGS handleJoin(ClientPtr client);

    void handlePart(ClientPtr client);

    void informBattleTerminated() {
        m_field = NULL;
    }

private:
    BattleChannel(Server *server,
            NetworkBattleImpl *field,
            const string &name,
            const string &topic,
            CHANNEL_FLAGS flags):
                Channel(server, name, topic, flags),
                m_server(server),
                m_field(field) { }

    Server *m_server;
    NetworkBattleImpl *m_field;
    boost::mutex m_mutex;
};

typedef vector<PokemonTurn> PARTY_TURN;
typedef vector<int> PARTY_REQUEST;
typedef boost::shared_ptr<PARTY_TURN> TURN_PTR;

struct NetworkBattleImpl {
    JewelMechanics mech;
    NetworkBattle *field;
    BattleChannelPtr channel;
    vector<ClientPtr> clients;
    vector<PARTY_TURN> turns;
    vector<PARTY_REQUEST> requests;
    ThreadedQueue<TURN_PTR> queue;
    boost::mutex mutex;
    bool replacement;
    bool victory;
    int turnCount;

    NetworkBattleImpl(Server *server, NetworkBattle *p):
            queue(boost::bind(&NetworkBattleImpl::executeTurn,
                    this, _1)),
            replacement(false),
            victory(false),
            field(p),
            turnCount(0),
            channel(BattleChannelPtr(
                BattleChannel::createChannel(server, this))) { }

    ~NetworkBattleImpl() {
        // we need to make sure the queue gets deconstructed first, since
        // its thread can reference this object
        queue.terminate();
    }

    void beginTurn() {
        ++turnCount;
        informBeginTurn();
        requestMoves();
    }
    
    void executeTurn(TURN_PTR &ptr) {
        if (replacement) {
            field->processReplacements(*ptr);
        } else {
            field->processTurn(*ptr);
        }
        if (!victory && !requestReplacements()) {
            beginTurn();
        }
    }
    
    void informBeginTurn() {
        OutMessage msg(OutMessage::BATTLE_BEGIN_TURN);
        msg << field->getId();
        msg << (int16_t)turnCount;
        msg.finalise();

        broadcast(msg);
    }

    void cancelAction(const int party) {
        PARTY_TURN &turn = turns[party];
        if (requests[party].size() == turn.size()) {
            // too late to cancel
            return;
        }

        turn.pop_back();
        requestAction(party);
    }

    void broadcast(OutMessage &msg) {
        channel->broadcast(msg);
    }

    ClientPtr getClient(const int idx) const {
        if (clients.size() <= idx)
            return ClientPtr();
        return clients[idx];
    }

    /**
     * BATTLE_BEGIN
     *
     * int32  : field id
     * string : opponent
     * byte   : party
     */
    void sendBattleBegin(const int party) {
        const int32_t id = field->getId();
        const string opponent = clients[1 - party]->getName();

        OutMessage msg(OutMessage::BATTLE_BEGIN);
        msg << id << opponent << ((unsigned char)party);
        msg.finalise();
        clients[party]->sendMessage(msg);
    }

    /**
     * BATTLE_POKEMON
     *
     * int32 : field id
     * for 0...1:
     *     for 0...n-1:
     *         int16 : species id
     *         if id != -1:
     *             byte : gender
     *             byte : whether the pokemon is shiny
     */
    void updateBattlePokemon() {
        OutMessage msg(OutMessage::BATTLE_POKEMON);
        msg << (int32_t)field->getId();

        const int size = field->getPartySize();
        boost::shared_ptr<PokemonParty> *active = field->getActivePokemon();
        for (int i = 0; i < TEAM_COUNT; ++i) {
            PokemonParty &party = *active[i];
            for (int j = 0; j < size; ++j) {
                Pokemon::PTR p = party[j].pokemon;
                if (p && !p->isFainted()) {
                    int16_t species = (int16_t)p->getSpeciesId();
                    msg << species;
                    msg << (unsigned char)p->getGender();
                    msg << (unsigned char)p->isShiny();
                } else {
                    msg << ((int16_t)-1);
                }
            }
        }
        msg.finalise();

        broadcast(msg);
    }

    /**
     * REQUEST_ACTION
     *
     * int32 : field id
     * byte  : slot of relevant pokemon
     * byte  : position of relevant pokemon
     * byte  : whether this is a replacement
     * int32 : number of pokemon
     * for each pokemon:
     *      byte : whether it is legal to switch to this pokemon
     * if not replacement:
     *      byte : whether switching is legal
     *      byte : whether there is a forced move
     *      if not forced:
     *          int32 : total number of moves
     *          for each move:
     *              byte : whether the move is legal
     */
    void requestAction(const int party) {
        PARTY_TURN &turn = turns[party];
        const int size = turn.size();
        const int slot = requests[party][size];
        Pokemon::PTR p = field->getActivePokemon(party, slot);
        
        OutMessage msg(OutMessage::REQUEST_ACTION);
        msg << field->getId();
        msg << (unsigned char)(p->getSlot());
        msg << (unsigned char)(p->getPosition());
        msg << (unsigned char)replacement;

        vector<bool> switches;
        field->getLegalSwitches(p.get(), switches);

        for (int i = 0; i < size; ++i) {
            PokemonTurn &t = turn[i];
            if (t.type == TT_SWITCH) {
                switches[t.id] = false;
            }
        }

        const int switchSize = switches.size();
        msg << switchSize;
        for (int i = 0; i < switchSize; ++i) {
            msg << ((unsigned char)switches[i]);
        }

        if (!replacement) {
            msg << (unsigned char)p->isSwitchLegal();

            const bool forced = (p->getForcedTurn() != NULL);
            msg << (unsigned char)forced;

            if (!forced) {
                const int moveCount = p->getMoveCount();
                msg << moveCount;
                for (int i = 0; i < moveCount; ++i) {
                    msg << (unsigned char)p->isMoveLegal(i);
                }
            }
        }
        msg.finalise();

        ClientPtr client = getClient(party);
        if (client) {
            client->sendMessage(msg);
        }
    }

    bool requestReplacements() {
        boost::lock_guard<boost::mutex> lock(mutex);

        Pokemon::ARRAY pokemon;
        field->getFaintedPokemon(pokemon);
        if (pokemon.empty()) {
            return false;
        }
        int alive[TEAM_COUNT];
        for (int i = 0; i < TEAM_COUNT; ++i) {
            alive[i] = field->getAliveCount(i);
        }
        replacement = false;
        for (Pokemon::ARRAY::const_iterator i = pokemon.begin();
                i != pokemon.end(); ++i) {
            const int party = (*i)->getParty();
            if (alive[party] > 1) {
                const int slot = (*i)->getSlot();
                requests[party].push_back(slot);
                replacement = true;
                --alive[party];
            }
        }
        if (!replacement) {
            return false;
        }
        for (int i = 0; i < TEAM_COUNT; ++i) {
            if (!requests[i].empty()) {
                requestAction(i);
            }
        }
        return true;
    }

    void requestMoves() {
        boost::lock_guard<boost::mutex> lock(mutex);

        replacement = false;
        Pokemon::ARRAY pokemon;
        field->getActivePokemon(pokemon);
        for (Pokemon::ARRAY::const_iterator i = pokemon.begin();
                i != pokemon.end(); ++i) {
            const int party = (*i)->getParty();
            const int slot = (*i)->getSlot();
            (*i)->determineLegalActions();
            requests[party].push_back(slot);
        }
        for (int i = 0; i < TEAM_COUNT; ++i) {
            requestAction(i);
        }
    }

    void maybeExecuteTurn() {
        const int count = requests.size();
        for (int i = 0; i < count; ++i) {
            if (requests[i].size() != turns[i].size()) {
                return;
            }
        }

        TURN_PTR turn(new PARTY_TURN());
        for (int i = 0; i < count; ++i) {
            PARTY_TURN &v = turns[i];
            turn->insert(turn->end(), v.begin(), v.end());
            v.clear();
            requests[i].clear();
        }
        
        queue.post(turn);
    }
};

Channel::FLAGS BattleChannel::handleJoin(ClientPtr client) {
    FLAGS ret;
    ChannelPtr p = m_server->getMainChannel();
    if (p) {
        FLAGS flags = p->getStatusFlags(client);
        if (flags[OP] || flags[OWNER]) {
            // user is a main chat op, so he gets +q
            ret[OWNER] = true;
        }
    }
    if (m_field && (m_field->field->getParty(client) != -1)) {
        // user is a participant in the battle, so he gets +ao
        ret[OP] = true;
        ret[PROTECTED] = true;
    }
    // todo: bans
    return ret;
}

void BattleChannel::handlePart(ClientPtr client) {
    boost::lock_guard<boost::mutex> lock(m_mutex);
    
    int party = -1;
    if (m_field && ((party = m_field->field->getParty(client)) != -1)) {
        // user was a participant in the battle, so we need to end the battle
        m_field->field->informVictory(1 - party);
    }

    // todo: destroy the channel if this is the last user
}

void NetworkBattle::terminate() {
    // note: once this variable 'p' goes out of scope, this object will die,
    // so it needs to be here to allow this method to work.
    NetworkBattle::PTR p = shared_from_this();

    boost::lock_guard<boost::mutex> lock(m_impl->mutex);

    // There will always be two clients in the vector at this point.
    m_impl->clients[0]->terminateBattle(p, m_impl->clients[1]);
    
    BattleField::terminate();
    m_impl->channel->informBattleTerminated();
}

NetworkBattle::NetworkBattle(Server *server,
        ClientPtr *clients,
        Pokemon::ARRAY *teams,
        const GENERATION generation,
        const int partySize) {
    m_impl = boost::shared_ptr<NetworkBattleImpl>(
            new NetworkBattleImpl(server, this));
    m_impl->turns.resize(TEAM_COUNT);
    m_impl->requests.resize(TEAM_COUNT);
    string trainer[TEAM_COUNT];
    for (int i = 0; i < TEAM_COUNT; ++i) {
        ClientPtr client = clients[i];
        trainer[i] = client->getName();
        m_impl->clients.push_back(client);
        client->joinChannel(m_impl->channel);
    }
    initialise(&m_impl->mech,
            generation, server->getMachine(), teams, trainer, partySize);
}

int32_t NetworkBattle::getId() const {
    return m_impl->channel->getId();
}

void NetworkBattle::beginBattle() {
    boost::unique_lock<boost::mutex> lock(m_impl->mutex);
    for (int i = 0; i < TEAM_COUNT; ++i) {
        m_impl->sendBattleBegin(i);
    }
    lock.unlock();
    BattleField::beginBattle();
    m_impl->beginTurn();
}

int NetworkBattle::getParty(boost::shared_ptr<network::Client> client) const {
    boost::lock_guard<boost::mutex> lock(m_impl->mutex);
    int count = m_impl->clients.size();
    if (count > 2) {
        count = 2;
    }
    for (int i =  0; i < count; ++i) {
        if (m_impl->clients[i] == client)
            return i;
    }
    return -1;
}

void NetworkBattle::handleCancelTurn(const int party) {
    boost::lock_guard<boost::mutex> lock(m_impl->mutex);
    m_impl->cancelAction(party);
}

void NetworkBattle::handleTurn(const int party, const PokemonTurn &turn) {
    boost::lock_guard<boost::mutex> lock(m_impl->mutex);

    PARTY_REQUEST &req = m_impl->requests[party];
    PARTY_TURN &pturn = m_impl->turns[party];
    const int max = req.size();
    const int present = pturn.size();
    if (present == max) {
        return;
    }
    const int slot = req[present];
    Pokemon *pokemon = getActivePokemon(party, slot).get();
    if (!isTurnLegal(pokemon, &turn, m_impl->replacement)) {
        // todo: inform illegal move?
        m_impl->requestAction(party);
        return;
    }
    if (turn.type == TT_SWITCH) {
        for (int i = 0; i < present; ++i) {
            PokemonTurn &t = pturn[i];
            if ((t.type == TT_SWITCH) && (t.id == turn.id)) {
                // inform illegal move
                m_impl->requestAction(party);
                return;
            }
        }
    }
    pturn.push_back(turn);
    if (pturn.size() < max) {
        m_impl->requestAction(party);
    } else {
        m_impl->maybeExecuteTurn();
    }
}

/**
 * BATTLE_PRINT
 *
 * int32 : field id
 * byte  : category
 * int16 : message id
 * byte  : number of arguments
 * for each argument:
 *     string : value of the argument
 */
void NetworkBattle::print(const TextMessage &text) {
    OutMessage msg(OutMessage::BATTLE_PRINT);
    msg << getId();
    msg << (unsigned char)text.getCategory();
    msg << (int16_t)text.getMessage();
    const vector<string> &args = text.getArgs();
    const int count = args.size();
    msg << (unsigned char)count;
    for (int i = 0; i < count; ++i) {
        msg << args[i];
    }
    msg.finalise();

    m_impl->broadcast(msg);
}

/**
 * BATTLE_VICTORY
 *
 * int32 : field id
 * int16 : party id (or -1 for a draw)
 */
void NetworkBattle::informVictory(const int party) {
    m_impl->victory = true;

    OutMessage msg(OutMessage::BATTLE_VICTORY);
    msg << getId();
    msg << (int16_t)party;
    msg.finalise();

    m_impl->broadcast(msg);

    // todo: adjust ratings etc.

    // terminate this battle
    terminate();
}

/**
 * BATTLE_USE_MOVE
 *
 * int32 : field id
 * byte : party
 * byte : slot
 * string : user [nick]name
 * int16 : move id
 */
void NetworkBattle::informUseMove(Pokemon *pokemon, MoveObject *move) {
    OutMessage msg(OutMessage::BATTLE_USE_MOVE);
    msg << getId();
    msg << (unsigned char)pokemon->getParty();
    msg << (unsigned char)pokemon->getSlot();
    msg << pokemon->getName();
    msg << (int16_t)move->getTemplate(getContext())->getId();
    msg.finalise();

    m_impl->broadcast(msg);
}

/**
 * BATTLE_WITHDRAW
 *
 * int32 : field id
 * byte : party
 * byte : slot
 * string : pokemon [nick]name
 */
void NetworkBattle::informWithdraw(Pokemon *pokemon) {
    OutMessage msg(OutMessage::BATTLE_WITHDRAW);
    msg << getId();
    msg << (unsigned char)pokemon->getParty();
    msg << (unsigned char)pokemon->getSlot();
    msg << pokemon->getName();
    msg.finalise();

    m_impl->broadcast(msg);
}

/**
 * BATTLE_SEND_OUT
 *
 * int32  : field id
 * byte   : party
 * byte   : slot
 * byte   : index
 * string : pokemon [nick]name
 */
void NetworkBattle::informSendOut(Pokemon *pokemon) {
    OutMessage msg(OutMessage::BATTLE_SEND_OUT);
    msg << getId();
    msg << (unsigned char)pokemon->getParty();
    msg << (unsigned char)pokemon->getSlot();
    msg << (unsigned char)pokemon->getPosition();
    msg << pokemon->getName();
    msg.finalise();

    m_impl->broadcast(msg);
    m_impl->updateBattlePokemon();
}

/**
 * BATTLE_HEALTH_CHANGE
 *
 * int32  : field id
 * byte   : party
 * byte   : slot
 * string : pokemon [nick]name
 * int16  : delta health in [0, 48]
 * int16  : new total health [0, 48]
 */
void NetworkBattle::informHealthChange(Pokemon *pokemon, const int raw) {
    const int hp = pokemon->getRawStat(S_HP);
    const int delta = 48.0 * (double)raw / (double)hp + 0.5;
    const int total = 48.0 * (double)pokemon->getHp() / (double)hp + 0.5;

    OutMessage msg(OutMessage::BATTLE_HEALTH_CHANGE);
    msg << getId();
    msg << (unsigned char)pokemon->getParty();
    msg << (unsigned char)pokemon->getSlot();
    msg << pokemon->getName();
    msg << (int16_t)delta;
    msg << (int16_t)total;
    msg.finalise();

    m_impl->broadcast(msg);
}

/**
 * BATTLE_SET_PP
 *
 * int32 : field id
 * byte  : pokemon index
 * byte  : move index
 * byte  : new pp value
 */
void NetworkBattle::informSetPp(Pokemon *pokemon,
        const int move, const int pp) {
    OutMessage msg(OutMessage::BATTLE_SET_PP);
    msg << getId();
    msg << (unsigned char)pokemon->getPosition();
    msg << (unsigned char)move;
    msg << (unsigned char)pp;
    msg.finalise();

    boost::lock_guard<boost::mutex> lock(m_impl->mutex);
    ClientPtr client = m_impl->getClient(pokemon->getParty());
    if (client) {
        client->sendMessage(msg);
    }
}

/**
 * BATTLE_FAINTED
 *
 * int32 : field id
 * byte : party
 * byte : slot
 * string : pokemon [nick]name
 */
void NetworkBattle::informFainted(Pokemon *pokemon) {
    OutMessage msg(OutMessage::BATTLE_FAINTED);
    msg << getId();
    msg << (unsigned char)pokemon->getParty();
    msg << (unsigned char)pokemon->getSlot();
    msg << pokemon->getName();
    msg.finalise();

    m_impl->broadcast(msg);
    m_impl->updateBattlePokemon();
}

}} // namespace shoddybattle::network
