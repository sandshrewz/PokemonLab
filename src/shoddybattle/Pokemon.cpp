/* 
 * File:   Pokemon.cpp
 * Author: Catherine
 *
 * Created on March 28, 2009, 12:12 AM
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

#include <iostream>
#include <list>
#include <sstream>
#include <boost/bind.hpp>

#include "Pokemon.h"
#include "PokemonSpecies.h"
#include "../mechanics/PokemonNature.h"
#include "../mechanics/PokemonType.h"
#include "../mechanics/JewelMechanics.h"
#include "../text/Text.h"
#include "../scripting/ScriptMachine.h"
#include "BattleField.h"

using namespace shoddybattle;
using namespace std;
using namespace boost;

namespace shoddybattle {

Pokemon::Pokemon(const PokemonSpecies *species,
        const string &nickname,
        const PokemonNature *nature,
        const string &ability,
        const string &item,
        const int *iv,
        const int *ev,
        const int level,
        const int gender,
        const bool shiny,
        const vector<string> &moves,
        const vector<int> &ppUps) {
    memcpy(m_iv, iv, sizeof(int) * STAT_COUNT);
    memcpy(m_ev, ev, sizeof(int) * STAT_COUNT);
    memset(m_statLevel, 0, sizeof(int) * TOTAL_STAT_COUNT);
    m_species = species;
    m_nickname = nickname;
    if (m_nickname.empty()) {
        m_nickname = m_species->getSpeciesName();
    } else if (m_nickname.length() > 19) {
        m_nickname = m_nickname.substr(0, 19);
    }
    m_nature = nature;
    m_types = species->getTypes();
    m_level = level;
    m_gender = gender;
    m_ppUps = ppUps;
    m_abilityName = ability;
    m_itemName = item;
    m_shiny = shiny;
    vector<string>::const_iterator i = moves.begin();
    for (; i != moves.end(); ++i) {
        const MoveTemplate *move = species->getMove(*i);
        if (move) {
            m_moveProto.push_back(move);
        }
    }
    m_pp.resize(m_moveProto.size());
    m_moveUsed.resize(m_moveProto.size(), false);
    m_machine = NULL;
    m_cx = NULL;
    m_field = NULL;
    m_fainted = false;
    m_legalSwitch = true;
    m_slot = -1;
    m_acted = false;
}

string Pokemon::getToken() const {
    stringstream ss;
    ss << "$p{" << getParty() << "," << getPosition() << "}";
    return ss.str();
}

double Pokemon::getMass() const {
    return m_species->getMass();
}

string Pokemon::getSpeciesName() const {
    return m_species->getSpeciesName();
}

int Pokemon::getSpeciesId() const {
    return m_species->getSpeciesId();
}

unsigned int Pokemon::getBaseStat(const STAT i) const {
    return m_species->getBaseStat(i);
}

/**
 * Determine whether the pokemon has a particular type.
 */
bool Pokemon::isType(const PokemonType *type) const {
    TYPE_ARRAY::const_iterator i = m_types.begin();
    for (; i != m_types.end(); ++i) {
        if (*i == type)
            return true;
    }
    return false;
}

/**
 * Determine the legal actions a pokemon can take this turn.
 */
void Pokemon::determineLegalActions() {
    m_legalSwitch = !m_field->vetoSwitch(this);

    bool struggle = true;
    const int count = m_moves.size();
    m_legalMove.clear();
    m_legalMove.resize(count, false);
    for (int i = 0; i < count; ++i) {
        MoveObjectPtr move = m_moves[i];
        if (move && (m_pp[i] > 0)) {
            if (m_legalMove[i] = !m_field->vetoSelection(this, move.get())) {
                struggle = false;
            }
        }
    }

    if (struggle) {
        setForcedTurn(m_machine->getMoveDatabase()->getMove("Struggle"), NULL);
    }
}

/**
 * Send an arbitrary message to this pokemon.
 */
ScriptValue Pokemon::sendMessage(const string &name,
        int argc, ScriptValue *argv) {
    ScriptValue ret;
    bool failed = true;
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        if (m_cx->hasProperty(i->get(), name)) {
            ret = m_cx->callFunctionByName(i->get(), name, argc, argv);
            failed = false;
        }
    }
    if (failed) {
        ret.setFailure();
    }
    return ret;
}

/**
 * Get additional immunities or vulnerabilities in play for a given user
 * attacking a given target.
 */
void Pokemon::getImmunities(Pokemon *user, Pokemon *target,
        std::set<const PokemonType *> &immunities,
        std::set<const PokemonType *> &vulnerabilities) {
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        const PokemonType *type = (*i)->getImmunity(m_cx, user, target);
        if (type) {
            immunities.insert(type);
        }
        type = (*i)->getVulnerability(m_cx, user, target);
        if (type) {
            if (immunities.find(type) != immunities.end()) {
                immunities.erase(type);
            } else {
                vulnerabilities.insert(type);
            }
        }
    }
}

/**
 * Determine whether the selection of a move should be vetoed.
 */
bool Pokemon::vetoSelection(Pokemon *user, MoveObject *move) {
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        if ((*i)->vetoSelection(m_cx, user, move))
            return true;
    }
    return false;
}

namespace {

bool vetoExecutionPredicate(ScriptContext *cx,
        StatusObjectPtr s1,
        StatusObjectPtr s2) {
    return (s1->getVetoTier(cx) < s2->getVetoTier(cx));
}

} // anonymous namespace

/**
 * Determine whether the execution of a move should be vetoed, according to
 * the effects on this pokemon.
 */
bool Pokemon::vetoExecution(Pokemon *user, Pokemon *target, MoveObject *move) {
    vector<StatusObjectPtr> effects;
    effects.insert(effects.begin(), m_effects.begin(), m_effects.end());
    sort(effects.begin(), effects.end(),
            boost::bind(vetoExecutionPredicate, m_cx, _1, _2));
    for (vector<StatusObjectPtr>::const_iterator i = effects.begin();
            i != effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        if ((*i)->vetoExecution(m_cx, m_field, user, target, move))
            return true;
    }
    return false;
}

/**
 * Send this pokemon out onto the field.
 */
void Pokemon::switchIn() {
    // Inform status effects of switching in.
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        (*i)->switchIn(m_cx);
    }
}

namespace {

bool switchOutPredicate(StatusObjectPtr effect, ScriptContext *cx) {
    if (!effect->isActive(cx))
        return false;
    if (!effect->switchOut(cx))
        return false;
    effect->unapplyEffect(cx);
    return true;
}

} // anonymous namespace

/**
 * Switch this pokemon out of the field.
 */
void Pokemon::switchOut() {
    // Remove effects that do not survive switches.
    removeStatuses(m_effects, boost::bind(switchOutPredicate, _1, m_cx));   
    // Restore original ability.
    setAbility(m_abilityName);
    // Indicate that the pokemon is no longer active.
    m_slot = -1;
    // Clear this pokemon's memory.
    m_memory.clear();
    m_moveUsed.clear();
    m_moveUsed.resize(m_moves.size(), false);
    m_lastMove.reset();
    m_acted = false;
    // Adjust the memories other active pokemon.
    clearMemory();
}

/**
 * Adjust the memories other active pokemon.
 */
void Pokemon::clearMemory() {
    shared_ptr<PokemonParty> *active = m_field->getActivePokemon();
    for (int i = 0; i < TEAM_COUNT; ++i) {
        PokemonParty &party = *active[i];
        const int size = party.getSize();
        for (int j = 0; j < size; ++j) {
            Pokemon::PTR p = party[j].pokemon;
            if (p) {
                p->removeMemory(this);
            }
        }
    }
}

/**
 * Get a status effect by ID.
 */
StatusObjectPtr Pokemon::getStatus(const string &id) {
    for (STATUSES::iterator i = m_effects.begin(); i != m_effects.end(); ++i) {
        if ((*i)->isRemovable(m_cx))
            continue;

        if ((*i)->getId(m_cx) == id)
            return *i;
    }
    return StatusObjectPtr();
}

/**
 * Get a status effect by lock.
 */
StatusObjectPtr Pokemon::getStatus(const int lock) {
    for (STATUSES::iterator i = m_effects.begin(); i != m_effects.end(); ++i) {
        if ((*i)->isRemovable(m_cx))
            continue;

        if ((*i)->getLock(m_cx) == lock)
            return *i;
    }
    return StatusObjectPtr();
}

/**
 * Execute an arbitrary move on a particular target.
 */
bool Pokemon::useMove(MoveObject *move,
        Pokemon *target, const int targets) {
    if (m_field->vetoExecution(this, target, move)) {
        // vetoed for this target
        return false;
    }
    if (move->attemptHit(m_cx, m_field, this, target)) {
        move->use(m_cx, m_field, this, target, targets);
    } else {
        vector<string> args;
        args.push_back(getToken());
        args.push_back(target->getToken());
        TextMessage msg(4, 2, args); // attack missed
        m_field->print(msg);
    }
    return true;
}

/**
 * Force the pokemon to carry out a particular turn next round.
 */
void Pokemon::setForcedTurn(const PokemonTurn &turn) {
    m_forcedTurn = shared_ptr<PokemonTurn>(new PokemonTurn(turn));
}

/**
 * Force the pokemon to use a particular move next round.
 */
MoveObjectPtr Pokemon::setForcedTurn(const MoveTemplate *move, Pokemon *p) {
    int target = p ? p->getSlot() : -1;
    if (p && (p->getParty() == 1)) {
        target += m_field->getPartySize();
    }
    m_forcedMove = m_cx->newMoveObject(move);
    setForcedTurn(PokemonTurn(TT_MOVE, -1, target));
    return m_forcedMove;
}

/**
 * Get the index of a named move, -1 if the pokemon does not know the move.
 */
int Pokemon::getMove(const string &name) const {
    const int size = m_moves.size();
    for (int i = 0; i < size; ++i) {
        if (m_moves[i]->getName(m_cx) == name)
            return i;
    }
    return -1;
}

/**
 * Transform a stat level according to this pokemon.
 */
bool Pokemon::getTransformedStatLevel(Pokemon *user, Pokemon *target,
        STAT stat, int *level) {
    for (STATUSES::iterator i = m_effects.begin(); i != m_effects.end(); ++i) {
        if ((*i)->isRemovable(m_cx))
            continue;

        if ((*i)->transformStatLevel(m_cx, user, target, stat, level))
            return true;
    }
    return false;
}

/**
 * Get the effective value of a stat.
 */
unsigned int Pokemon::getStat(const STAT stat) {
    if (stat == S_HP)
        return m_stat[stat];
    PRIORITY_MAP mods;
    m_field->getStatModifiers(stat, this, NULL, mods);
    mods[0] = getStatMultiplier(stat, m_statLevel[stat]);
    int value = getRawStat(stat);
    PRIORITY_MAP::const_iterator i = mods.begin();
    for (; i != mods.end(); ++i) {
        double val = i->second;
        value *= val;
    }
    return value;
}

/**
 * Get a move by index, or -1 for the pokemon's forced move.
 */
MoveObjectPtr Pokemon::getMove(const int i) {
    if (i == -1)
        return m_forcedMove;
    if (m_moves.size() <= i)
        return MoveObjectPtr();
    return m_moves[i];
}

/**
 * Execute an arbitrary move on a set of targets.
 */
bool Pokemon::executeMove(MoveObjectPtr move,
        Pokemon *target, bool inform) {

    if (inform) {
        m_lastMove.reset();
        if (m_field->vetoExecution(this, NULL, move.get())) {
            // vetoed
            m_acted = true;
            return false;
        }
    }

    m_field->informUseMove(this, move.get());

    if (move->getFlag(m_cx, F_UNIMPLEMENTED)) {
        cout << "But it's unimplemented..." << endl;
        m_acted = true;
        return false;
    }

    TARGET tc = move->getTargetClass(m_cx);

    if (tc == T_USER) {
        move->use(m_cx, m_field, this, NULL, 0);
        m_acted = true;
        return true;
    }
    
    // Build a list of targets.
    vector<Pokemon *> targets;
    m_field->getTargetList(tc, targets, this, target);

    if (tc == T_NONE) {
        move->use(m_cx, m_field, this, targets[0], 0);
        m_acted = true;
        return true;
    }

    int targetCount = targets.size();
    if (targetCount == 0) {
        m_field->print(TextMessage(4, 3)); // no target
        m_acted = true;
        return true;
    }

    if (inform) {
        for (vector<Pokemon *>::iterator i = targets.begin();
                i != targets.end(); ++i) {
            Pokemon *p = *i;
            if (p) {
                p->informTargeted(this, move);
            }
        }
    }

    BattleField::EXECUTION entry = { this, move };
    m_field->pushExecution(entry);

    move->prepareSelf(m_cx, m_field, this);

    if (isEnemyTarget(tc)) {
        for (vector<Pokemon *>::iterator i = targets.begin();
                i != targets.end(); ++i) {
            Pokemon *p = *i;
            if (p) {
                useMove(move.get(), p, targetCount);
                if (p->isFainted()) {
                    --targetCount;
                }
            }
        }
    } else {
        // no target as such
        move->use(m_cx, m_field, this, NULL, 0);
    }

    m_field->popExecution();

    m_acted = true;
    return true;
}

/**
 * Remove one of this pokemon's memories, perhaps due to a pokemon leaving
 * the field.
 */
void Pokemon::removeMemory(Pokemon *pokemon) {
    MEMORY entry = { pokemon, MoveObjectPtr() };
    m_memory.remove(entry);
}

/**
 * Set one of this pokemon's moves to a different move.
 */
void Pokemon::setMove(const int i, MoveObjectPtr move, const int pp) {
    if (m_moves.size() <= i) {
        m_moves.resize(i + 1, MoveObjectPtr());
        m_pp.resize(i + 1, 0);
        m_moveUsed.resize(i + 1, false);
    }
    m_moves[i] = move;
    m_pp[i] = pp;
}

/**
 * Set one of this pokemon's moves to a different move, by name. This
 * function will probably only be used for testing, as actual logic is more
 * likely to be use the MoveObject * version above.
 */
void Pokemon::setMove(const int i, const string &name, const int pp) {
    ScriptContext *cx = m_field->getContext();
    MoveDatabase *moves = m_machine->getMoveDatabase();
    const MoveTemplate *tpl = moves->getMove(name);
    assert(tpl);
    MoveObjectPtr move = cx->newMoveObject(tpl);
    setMove(i, move, pp);
}

/**
 * Remove defunct statuses from this pokemon.
 */
void Pokemon::removeStatuses() {
    removeStatuses(m_effects,
            boost::bind(&StatusObject::isRemovable, _1, m_cx));
}

template <class T>
void Pokemon::removeStatuses(STATUSES &effects, T predicate) {
    bool removed = false;
    do {
        for (STATUSES::iterator i = effects.begin();
                i != effects.end(); ++i) {
            if (!predicate(*i))
                continue;

            effects.erase(i);
            removed = true;
            break;
        }
    } while (removed && !(removed = false));
}

/**
 * Return whether the pokemon has the specified ability.
 */
bool Pokemon::hasAbility(const string &name) {
    if (!m_ability)
        return false;
    return (m_ability->getId(m_cx) == name);
}

/**
 * Apply a StatusEffect to this pokemon. Makes a copy of the parameter before
 * applying it to the pokemon.
 */
StatusObjectPtr Pokemon::applyStatus(Pokemon *inducer, StatusObject *effect) {
    if (!effect)
        return StatusObjectPtr();

    const int lock = effect->getLock(m_cx);
    if ((lock != 0) && getStatus(lock)) {
        return StatusObjectPtr();
    }

    if (effect->isSingleton(m_cx)) {
        StatusObjectPtr singleton = getStatus(effect->getId(m_cx));
        if (singleton)
            return StatusObjectPtr();
    }

    StatusObjectPtr applied = effect->cloneAndRoot(m_cx);
    if (inducer) {
        applied->setInducer(m_cx, inducer);
    }
    applied->setSubject(m_cx, this);

    m_field->transformStatus(this, &applied);
    if (!applied || !applied->applyEffect(m_cx)) {
        return StatusObjectPtr();
    }

    m_effects.push_back(applied);
    return applied;
}

/**
 * Transform a StatusEffect.
 */
bool Pokemon::transformStatus(Pokemon *subject, StatusObjectPtr *status) {
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        if ((*i)->transformStatus(m_cx, subject, status)) {
            if (*status == NULL) {
                return true;
            }
        }
    }
    return true;
}

/**
 * Remove a StatusEffect from this pokemon.
 */
void Pokemon::removeStatus(StatusObject *status) {
    status->unapplyEffect(m_cx);
    status->dispose(m_cx);
}

/**
 * Get critical hit chance (additive) modifiers.
 */
int Pokemon::getCriticalModifier() const {
    int ret = 0;
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        ret += (*i)->getCriticalModifier(m_cx);
    }
    return ret;
}

/**
 * Transform a health change.
 */
int Pokemon::transformHealthChange(int hp, bool indirect) const {
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        (*i)->transformHealthChange(m_cx, hp, indirect, &hp);
    }
    return hp;
}

/**
 * Check if this Pokemon has "inherent priority". This is used for certain
 * items and abilities.
 */
int Pokemon::getInherentPriority() const {
    int ret = 0;
    for (STATUSES::const_iterator i = m_effects.begin();
            i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        int v = (*i)->getInherentPriority(m_cx);
        if (abs(v) > abs(ret)) {
            ret = v;
        }
    }
    return ret;
}

/**
 * Check for stat modifiers on all status effects.
 */
void Pokemon::getStatModifiers(STAT stat,
        Pokemon *subject, Pokemon *target, PRIORITY_MAP &mods) {
    MODIFIER mod;
    for (STATUSES::iterator i = m_effects.begin(); i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        if ((*i)->getStatModifier(m_cx, m_field, stat, subject, target, mod)) {
            // position unused
            mods[mod.priority] = mod.value;
        }
    }
}

/**
 * Check for modifiers on all status effects.
 */
void Pokemon::getModifiers(Pokemon *user, Pokemon *target,
        MoveObject *obj, const bool critical, const int targets,
        MODIFIERS &mods) {
    MODIFIER mod;
    for (STATUSES::iterator i = m_effects.begin(); i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        if ((*i)->getModifier(m_cx, m_field,
                user, target, obj, critical, targets, mod)) {
            mods[mod.position][mod.priority] = mod.value;
        }
    }
}

/**
 * Cause this pokemon to faint.
 */
void Pokemon::faint() {
    m_fainted = true;
    m_field->informFainted(this);
    // TODO: Clear memory at end of move execution instead.
    clearMemory();
}

/**
 * Set the current hp of the pokemon, and also inform the BattleField, which
 * can cause side effects such as the printing of messages.
 */
void Pokemon::setHp(int hp) {
    // TODO: Somehow handle the fact that being hit at 1 HP and saved by
    //       Focus Band counts as being hit for the purpose of several
    //       interactions.
    if (m_fainted) {
        return;
    }
    const int max = m_stat[S_HP];
    if (hp > max) {
        hp = max;
    }
    const BattleField::EXECUTION *move = m_field->topExecution();
    const bool indirect = !move || (move->user == this);
    const int delta = transformHealthChange(m_hp - hp, indirect);
    if (delta == 0) {
        return;
    }
    m_hp -= delta;
    m_field->informHealthChange(this, delta);
    if (move && (delta > 0)) {
        ScriptValue argv[] = { move, this };
        move->user->sendMessage("informDamaging", 2, argv);
        informDamaged(move->user, move->move, delta);
    }
    if (m_hp <= 0) {
        faint();
    }
}

/**
 * Get the last pokemon that targeted this pokemon with a move.
 */
Pokemon *Pokemon::getMemoryPokemon() const {
    if (m_memory.empty())
        return NULL;
    return m_memory.back().user;
}

/**
 * Get this pokemon's memory of a move that targeted it.
 */
MoveObjectPtr Pokemon::getMemory() const {
    if (m_memory.empty())
        return MoveObjectPtr();
    return m_memory.back().move;
}

/**
 * Inform that this pokemon was damaged by a move.
 */
void Pokemon::informDamaged(Pokemon *user, MoveObjectPtr move, int damage) {
    RECENT_DAMAGE entry = { user, move, damage };
    m_recent.push(entry);
    ScriptValue argv[] = { user, move.get(), damage };
    sendMessage("informDamaged", 3, argv);
}

/**
 * Inform that this pokemon was targeted by a move.
 */
void Pokemon::informTargeted(Pokemon *user, MoveObjectPtr move) {
    for (STATUSES::iterator i = m_effects.begin(); i != m_effects.end(); ++i) {
        if (!(*i)->isActive(m_cx))
            continue;

        (*i)->informTargeted(m_cx, user, move.get());
    }

    if (move->getFlag(m_cx, F_MEMORABLE)) {
        list<MEMORY>::iterator i = m_memory.begin();
        for (; i != m_memory.end(); ++i) {
            if (i->user == user) {
                m_memory.erase(i);
                break;
            }
        }
        const MEMORY entry = { user, move };
        m_memory.push_back(entry);
    }
}

/**
 * Set this pokemon's ability.
 */
void Pokemon::setAbility(StatusObject *obj) {
    if (m_ability) {
        removeStatus(m_ability.get());
    }
    m_ability = applyStatus(NULL, obj);
}

/**
 * Set this pokemon's ability by name.
 */
void Pokemon::setAbility(const string &name) {
    StatusObject ability = m_cx->getAbility(name);
    if (!ability.isNull()) {
        setAbility(&ability);
    } else {
        cout << "No such ability: " << name << "." << endl;
    }
}

/**
 * Set this pokemon's item.
 */
void Pokemon::setItem(StatusObject *obj) {
    if (m_item) {
        removeStatus(m_item.get());
    }
    m_item = applyStatus(NULL, obj);
}

/**
 * Set this pokemon's item by name.
 */
void Pokemon::setItem(const string &name) {
    StatusObject item = m_cx->getItem(name);
    if (!item.isNull()) {
        setItem(&item);
    } else {
        cout << "No such item: " << name << "." << endl;
    }
}

/**
 * Deduct PP from a move.
 */
void Pokemon::deductPp(MoveObjectPtr move) {
    void *p = move->getObject();
    int j = 0;
    vector<MoveObjectPtr>::const_iterator i = m_moves.begin();
    for (; i != m_moves.end(); ++i) {
        if (p == (*i)->getObject()) {
            deductPp(j);
            return;
        }
        ++j;
    }
}

/**
 * Deduct PP from a move slot.
 */
void Pokemon::deductPp(const int i) {
    --m_pp[i];
    if (m_pp[i] < 0) {
        m_pp[i] = 0;
    }
    m_moveUsed[i] = true;
    m_field->informSetPp(this, i, m_pp[i]);
}

void Pokemon::initialise(BattleField *field, ScriptContextPtr cx,
        const int party, const int idx) {
    m_field = field;
    m_party = party;
    m_position = idx;
    const BattleMechanics *mech = field->getMechanics();
    for (int i = 0; i < STAT_COUNT; ++i) {
        m_stat[i] = mech->calculateStat(*this, (STAT)i);
        m_statLevel[i] = 0;
    }
    m_statLevel[S_ACCURACY] = 0;
    m_statLevel[S_EVASION] = 0;

    // Set initial hp.
    m_hp = m_stat[S_HP];

    // Get script machine.
    m_machine = field->getScriptMachine();
    m_scx = cx;
    m_cx = m_scx.get();

    // Create pokemon object.
    m_object = m_cx->newPokemonObject(this);

    // Create move objects.
    vector<const MoveTemplate *>::const_iterator i = m_moveProto.begin();
    int j = 0;
    for (; i != m_moveProto.end(); ++i) {
        MoveObjectPtr obj = m_cx->newMoveObject(*i);
        m_moves.push_back(obj);
        m_pp[j] = obj->getPp(m_cx) * (5 + m_ppUps[j]) / 5;
        ++j;
    }

    // Create ability and item objects.
    setAbility(m_abilityName);
    if (!m_itemName.empty()) {
        setItem(m_itemName);
    }
}

}
