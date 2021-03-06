/* 
 * File:   PokemonSpecies.cpp
 * Author: Catherine
 *
 * Created on March 30, 2009, 8:40 PM
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

#include <set>
#include <map>
#include <string>
#include <iostream>
#include <boost/bind.hpp>

#include <xercesc/parsers/XercesDOMParser.hpp>
#include <xercesc/dom/DOM.hpp>
#include <xercesc/sax/HandlerBase.hpp>
#include <xercesc/util/XMLString.hpp>
#include <xercesc/util/PlatformUtils.hpp>

#include "PokemonSpecies.h"
#include "../mechanics/PokemonType.h"
#include "../mechanics/PokemonNature.h"
#include "../moves/PokemonMove.h"
#include "../scripting/ScriptMachine.h"
#include "../main/Log.h"

using namespace std;
using namespace xercesc;

namespace shoddybattle {

const string PokemonSpecies::m_restricted[] = { "Arceus", "Articuno",
        "Azelf", "Celebi", "Cresselia", "Darkrai","Deoxys", "Deoxys-f", 
        "Groudon", "Heatran", "Ho-oh", "Jirachi","Kyogre", "Latias", "Latios", 
        "Lugia", "Manaphy", "Mesprit", "Mew","Mewtwo", "Moltres", "Palkia", 
        "Raikou", "Rayquaza", "Regice", "Regigigas", "Regirock", "Registeel", 
        "Shaymin", "Suicune", "Unown", "Uxie", "Zapdos" };

bool PokemonSpecies::hasRestrictedIvs() const { 
    const int size = sizeof(m_restricted) / sizeof(string);
    for (int i = 0; i < size; ++i) {
        if (m_restricted[i] == m_name)
            return true;
    }
    return false;
}

const int ORIGIN_COUNT = 7;

/**
 * Names for types used within the XML format.
 */
typedef pair<string, STAT> STAT_PAIR;
STAT_PAIR statNames[] = {
    STAT_PAIR("hp", S_HP),
    STAT_PAIR("atk", S_ATTACK),
    STAT_PAIR("def", S_DEFENCE),
    STAT_PAIR("spd", S_SPEED),
    STAT_PAIR("satk", S_SPATTACK),
    STAT_PAIR("sdef", S_SPDEFENCE),
};

/**
 * Names for move origins used within the XML format.
 */
typedef pair<string, MOVE_ORIGIN> ORIGIN_PAIR;
ORIGIN_PAIR originNames[] = {
    ORIGIN_PAIR("level", MO_LEVEL),
    ORIGIN_PAIR("egg", MO_EGG),
    ORIGIN_PAIR("tutor", MO_TUTOR),
    ORIGIN_PAIR("machine", MO_MACHINE),
    ORIGIN_PAIR("event", MO_EVENT),
    ORIGIN_PAIR("pikalightball", MO_LIGHT_BALL),
    ORIGIN_PAIR("prevevo", MO_EVOLUTION),
};

template <typename T>
T getValueByName(pair<string, T> *pairs, int count, const string &name) {
    for (int i = 0; i < count; ++i) {
        pair<string, T> &p = pairs[i];
        if (p.first == name) {
            return p.second;
        }
    }
    return (T)-1;
}

struct SPECIES {
    int id;
    string name;
    vector<string> types;
    int gender;
    int base[6];
    double mass;
    map<MOVE_ORIGIN, set<string> > moves;
    COMBINATION_LIST illegal;
    ABILITY_LIST abilities;
};

class ShoddyHandler : public HandlerBase {
    void error(const SAXParseException& e) {
        fatalError(e);
    }
    void fatalError(const SAXParseException& e) {
        const XMLFileLoc line = e.getLineNumber();
        const XMLFileLoc column = e.getColumnNumber();
        Log::out() << "Error at (" << line << "," << column << ")." << endl;
        char *message = XMLString::transcode(e.getMessage());
        Log::out() << message << endl;
        XMLString::release(&message);
    }
};

int getIntNodeValue(DOMNode *node) {
    char *p = XMLString::transcode(node->getNodeValue());
    int i = atoi(p);
    XMLString::release(&p);
    return i;
}

string getStringNodeValue(DOMNode *node, bool text = false) {
    const XMLCh *str;
    DOMNode::NodeType type = node->getNodeType();
    if (text && (type == DOMNode::TEXT_NODE)) {
        str = ((DOMText *)node)->getWholeText();
    } else {
        str = node->getNodeValue();
    }
    char *p = XMLString::transcode(str);
    if (p == NULL)
        return string();
    string s = p;
    XMLString::release(&p);
    return s;
}

string getTextFromElement(DOMElement *element, bool text = false) {
    DOMNodeList *list = element->getChildNodes();
    if (list->getLength() > 0) {
        DOMNode *item = list->item(0);
        return getStringNodeValue(item, text);
    }
    return string();
}

string &lowercase(string &str) {
    string::iterator i = str.begin();
    for (; i != str.end(); ++i) {
        *i = tolower(*i);
    }
    return str;
}

int genderFromName(const string &txt) {
    int gender = 0;
    if (txt == "both") {
        gender = G_BOTH;
    } else if (txt == "male") {
        gender = G_MALE;
    } else if (txt == "female") {
        gender = G_FEMALE;
    } else if (txt == "none") {
        gender = G_NONE;
    } else {
        Log::out() << "Unknown gender: " << txt << endl;
    }
    return gender;
}

void getSpecies(DOMElement *node, SPECIES *pSpecies) {
    DOMNamedNodeMap *attributes = node->getAttributes();
    XMLCh tempStr[20];

    // id
    XMLString::transcode("id", tempStr, 19);
    DOMNode *p = attributes->getNamedItem(tempStr);
    int id = p ? getIntNodeValue(p) : -1;
    pSpecies->id = id;

    // name
    XMLString::transcode("name", tempStr, 19);
    p = attributes->getNamedItem(tempStr);
    if (p) {
        pSpecies->name = getStringNodeValue((DOMText *)p);
    }

    // types
    XMLString::transcode("type", tempStr, 19);
    DOMNodeList *list = node->getElementsByTagName(tempStr);
    int length = list->getLength();
    for (int i = 0; i < length; ++i) {
        DOMElement *item = (DOMElement *)list->item(i);
        string type = getTextFromElement(item);
        pSpecies->types.push_back(type);
    }

    // gender
    XMLString::transcode("gender", tempStr, 19);
    list = node->getElementsByTagName(tempStr);
    if (list->getLength() != 0) {
        string txt = getTextFromElement((DOMElement *)list->item(0));
        lowercase(txt);        
        pSpecies->gender = genderFromName(txt);
    }

    // stats
    XMLString::transcode("stats", tempStr, 19);
    list = node->getElementsByTagName(tempStr);
    if (list->getLength() != 0) {
        DOMElement *stats = (DOMElement *)list->item(0);

        // mass
        XMLString::transcode("mass", tempStr, 19);
        list = stats->getElementsByTagName(tempStr);
        if (list->getLength() != 0) {
            string txt = getTextFromElement((DOMElement *)list->item(0));
            double mass = atof(txt.c_str());
            pSpecies->mass = mass;
        }

        // base stats
        XMLString::transcode("base", tempStr, 19);
        list = stats->getElementsByTagName(tempStr);
        length = list->getLength();
        XMLString::transcode("stat", tempStr, 19);
        for (int i = 0; i < length; ++i) {
            DOMElement *stat = (DOMElement *)list->item(i);
            DOMNamedNodeMap *parts = stat->getAttributes();
            DOMNode *theStat = parts->getNamedItem(tempStr);
            if (theStat) {
                string strStat = getStringNodeValue((DOMText *)theStat);
                lowercase(strStat);
                STAT s = getValueByName(statNames, STAT_COUNT, strStat);
                if (s != S_NONE) {
                    string strElement = getTextFromElement(stat);
                    if (!strElement.empty()) {
                        int value = atoi(strElement.c_str());
                        pSpecies->base[s] = value;
                    }
                }
            }
        }
    }

    // abilities
    XMLString::transcode("abilities", tempStr, 19);
    list = node->getElementsByTagName(tempStr);
    if (list->getLength() != 0) {
        DOMElement *abilities = (DOMElement *)list->item(0);
        XMLString::transcode("ability", tempStr, 19);
        list = abilities->getElementsByTagName(tempStr);
        length = list->getLength();
        for (int i = 0; i < length; ++i) {
            DOMElement *ability = (DOMElement *)list->item(i);
            string txt = getTextFromElement(ability);
            pSpecies->abilities.push_back(txt);
        }
    }

    // moveset
    XMLString::transcode("moveset", tempStr, 19);
    list = node->getElementsByTagName(tempStr);
    if (list->getLength() != 0) {
        DOMElement *element = (DOMElement *)list->item(0);
        XMLString::transcode("moves", tempStr, 19);
        list = element->getElementsByTagName(tempStr);
        length = list->getLength();
        for (int i = 0; i < length; ++i) {
            DOMElement *moves = (DOMElement *)list->item(i);
            DOMNamedNodeMap *parts = moves->getAttributes();
            XMLString::transcode("origin", tempStr, 19);
            DOMNode *pOrigin = parts->getNamedItem(tempStr);
            if (pOrigin == NULL)
                continue;
            string origin = getStringNodeValue((DOMText *)pOrigin);
            MOVE_ORIGIN v = getValueByName(originNames, ORIGIN_COUNT, origin);
            if (v == MO_NONE)
                continue;

            set<string> moveSet;

            XMLString::transcode("move", tempStr, 19);
            DOMNodeList *list2 = moves->getElementsByTagName(tempStr);
            int length2 = list2->getLength();
            for (int j = 0; j < length2; ++j) {
                DOMElement *move = (DOMElement *)list2->item(j);
                string strMove = getTextFromElement(move);
                if (!strMove.empty()) {
                    moveSet.insert(strMove);
                }
            }
            pSpecies->moves[v] = moveSet;
        }
    }

    // illegal moves
    XMLString::transcode("illegal", tempStr, 19);
    list = node->getElementsByTagName(tempStr);
    COMBINATION_LIST illegal;
    if (list->getLength() != 0) {
        DOMElement *element = (DOMElement *)list->item(0);
        XMLString::transcode("combo", tempStr, 19);
        list = element->getElementsByTagName(tempStr);
        length = list->getLength();
        for (int i = 0; i < length; ++i) {
            DOMElement *comboNode = (DOMElement *)list->item(i);

            Combination combo;
            XMLString::transcode("move", tempStr, 19);
            DOMNodeList *list2 = comboNode->getElementsByTagName(tempStr);
            int length2 = list2->getLength();
            for (int j = 0; j < length2; ++j) {
                DOMElement *move = (DOMElement *)list2->item(j);
                string strMove = getTextFromElement(move);
                if (!strMove.empty()) {
                    combo.moves.push_back(strMove);
                }
            }

            // nature
            XMLString::transcode("nature", tempStr, 19);
            list2 = comboNode->getElementsByTagName(tempStr);
            combo.nature = NULL;
            if (list2->getLength() != 0) {
                string txt = getTextFromElement((DOMElement *)list2->item(0));
                combo.nature = PokemonNature::getNatureByCanonicalName(txt);
            }

            // ability
            XMLString::transcode("ability", tempStr, 19);
            list2 = comboNode->getElementsByTagName(tempStr);
            if (list2->getLength() != 0) {
                string txt = getTextFromElement((DOMElement *)list2->item(0));
                combo.ability = txt;
            }

            // gender
            XMLString::transcode("gender", tempStr, 19);
            list2 = comboNode->getElementsByTagName(tempStr);
            combo.gender = 0;
            if (list2->getLength() != 0) {
                string txt = getTextFromElement((DOMElement *)list2->item(0));
                lowercase(txt);
                combo.gender = genderFromName(txt);
            }

            illegal.push_back(combo);
        }
        pSpecies->illegal = illegal;
    }
}

bool PokemonSpecies::loadSpecies(const string file, SpeciesDatabase &set) {
    XMLPlatformUtils::Initialize();
    XercesDOMParser parser;
    //parser.setDoSchema(true);
    //parser.setValidationScheme(AbstractDOMParser::Val_Always);

    ShoddyHandler handler;
    parser.setErrorHandler(&handler);
    parser.setEntityResolver(&handler);

    parser.parse(file.c_str());

    DOMDocument *doc = parser.getDocument();
    DOMElement *root = doc->getDocumentElement();

    XMLCh tempStr[12];
    XMLString::transcode("species", tempStr, 11);
    DOMNodeList *list = root->getElementsByTagName(tempStr);

    int length = list->getLength();
    for (int i = 0; i < length; ++i) {
        DOMElement *item = (DOMElement *)list->item(i);
        SPECIES species;
        getSpecies(item, &species);
        if (set.m_set.find(species.id) != set.m_set.end()) {
            Log::out() << "Warning: Duplicate species ID: " << species.id << endl;
            continue;
        }
        PokemonSpecies *p = new PokemonSpecies((void *)&species);
        set.m_set[species.id] = p;
    }

    return true;
}

/**
 * Internally construct a new PokemonSpecies from a SPECIES structure.
 */
PokemonSpecies::PokemonSpecies(void *raw) {
    SPECIES *p = (SPECIES *)raw;
    m_name = p->name;
    m_id = p->id;
    m_gender = p->gender;
    memcpy(m_base, p->base, sizeof(int) * STAT_COUNT);
    m_moveset = p->moves;
    m_mass = p->mass;
    m_illegal = p->illegal;
    m_abilities = p->abilities;
    vector<string>::iterator i = p->types.begin();
    for (; i != p->types.end(); ++i) {
        const PokemonType *type = PokemonType::getByCanonicalName(*i);
        if (type == NULL) {
            Log::out() << "Unknown type: " << *i << endl;
            continue;
        }
        m_types.push_back(type);
    }
}

/**
 * Pokemon this pokemon's move list.
 */
set<string> PokemonSpecies::populateMoveList(const MoveDatabase &p) {
    set<string> ret;
    m_moves.clear();
    for (int i = 0; i < ORIGIN_COUNT; ++i) {
        set<string> &moves = m_moveset[(MOVE_ORIGIN)i];
        set<string>::iterator j = moves.begin();
        for (; j != moves.end(); ++j) {
            const string name = *j;
            const MoveTemplate *move = p.getMove(name);
            if (move) {
                m_moves[name] = move;
            } else {
                ret.insert(name);
            }
        }
    }
    return ret;
}

/**
 * Verify that every ability is implemented.
 */
void SpeciesDatabase::verifyAbilities(ScriptMachine *machine) const {
    set<string> abilities;
    SPECIES_SET::iterator i = m_set.begin();
    for (; i != m_set.end(); ++i) {
        const ABILITY_LIST &part = i->second->getAbilities();
        ABILITY_LIST::const_iterator j = part.begin();
        for (; j != part.end(); ++j) {
            abilities.insert(*j);
        }
    }
    Log::out() << "Unimplemented abilities:" << endl;
    int implemented = 0;
    ScriptContextPtr cx = machine->acquireContext();
    set<string>::const_iterator j = abilities.begin();
    for (; j != abilities.end(); ++j) {
        if (cx->getAbility(*j).isNull()) {
            Log::out() << "    " << *j << endl;
        } else {
            ++implemented;
        }
    }
    Log::out() << implemented << " / " << abilities.size()
            << " abilities implemented." << endl;
}

} // namespace shoddybattle

#include <ctime>
#include "../text/Text.h"

using namespace shoddybattle;

/**int main() {
    Text text("languages/english.lang");

    clock_t start = clock();
    SpeciesDatabase set("resources/species.xml");
    clock_t finish = clock();
    clock_t delta = finish - start;
    Log::out() << "Processed species.xml in " << delta << " nanoseconds." << endl;

    const PokemonSpecies *p = set.getSpecies("Rhydon");
    if (p) {
        MOVESET moveset = p->getMoveset();
        for (int i = 0; i < ORIGIN_COUNT; ++i) {
            Log::out() << originNames[i].first << ":" << endl;
            const vector<string> &moves = moveset[(MOVE_ORIGIN)i];
            vector<string>::const_iterator j = moves.begin();
            for (; j != moves.end(); ++j) {
                Log::out() << "    " << *j << endl;
            }
        }
    }
}**/

