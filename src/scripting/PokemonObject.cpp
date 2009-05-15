/* 
 * File:   PokemonObject.cpp
 * Author: Catherine
 *
 * Created on April 4, 2009, 1:55 AM
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

/**
 * This file implements the JavaScript interface to a Pokemon object.
 */

#include <stdlib.h>
#include <nspr/nspr.h>
#include <jsapi.h>

#include "ScriptMachine.h"
#include "../shoddybattle/Pokemon.h"
#include "../shoddybattle/BattleField.h"
#include "../mechanics/PokemonType.h"

#include <iostream>
#include <cmath>
#include <sstream>

using namespace std;

namespace shoddybattle {

namespace {

enum POKEMON_TINYID {
    PTI_SPECIES,
    PTI_NAME,
    PTI_BASE,
    PTI_IV,
    PTI_EV,
    PTI_STAT,
    PTI_LEVEL,
    PTI_NATURE,
    PTI_HP,     // modifiable
    PTI_TYPES,
    PTI_PPUPS,
    PTI_GENDER,
    PTI_MEMORY,
    PTI_FIELD,
    PTI_PARTY,
    PTI_POSITION,
    PTI_MOVE_COUNT,
    PTI_FAINTED,
    PTI_MASS
};

JSBool applyStatus(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    *ret = JSVAL_NULL;
    if (argc != 2) {
        JS_ReportError(cx, "applyStatus: wrong number of arguments");
        return JS_FALSE;
    }
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    if (JSVAL_IS_OBJECT(argv[1])) {
        StatusObject status(JSVAL_TO_OBJECT(argv[1]));
        Pokemon *inducer = NULL;
        if (JSVAL_IS_OBJECT(argv[0])) {
            inducer = (Pokemon *)JS_GetPrivate(cx, JSVAL_TO_OBJECT(argv[0]));
        }
        StatusObject *objret = p->applyStatus(inducer, &status);
        if (objret) {
            *ret = OBJECT_TO_JSVAL(objret->getObject());
        }
    }
    return JS_TRUE;
}

JSBool popRecentDamage(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    if (p->hasRecentDamage()) {
        ScriptContext *scx = (ScriptContext *)JS_GetContextPrivate(cx);
        Pokemon::RECENT_DAMAGE entry = p->popRecentDamage();

        JSObject *user = (JSObject *)entry.user->getObject()->getObject();
        MoveObject *pMove = scx->newMoveObject(entry.move);
        JSObject *move = (JSObject *)pMove->getObject();
        const int damage = entry.damage;

        jsval arr[3];
        arr[0] = OBJECT_TO_JSVAL(user);
        arr[1] = OBJECT_TO_JSVAL(move);
        arr[2] = INT_TO_JSVAL(damage);

        JSObject *objArr = JS_NewArrayObject(cx, 3, arr);
        *ret = OBJECT_TO_JSVAL(objArr);
        scx->removeRoot(pMove);

    } else {
        *ret = JSVAL_NULL;
    }
    return JS_TRUE;
}

JSBool removeStatus(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    *ret = JSVAL_NULL;
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    jsval v = argv[0];
    assert(JSVAL_IS_OBJECT(v));
    StatusObject status(JSVAL_TO_OBJECT(v));
    p->removeStatus(&status);
    return JS_TRUE;
}

/**
 * pokemon.isType(type)
 */
JSBool isType(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);

    jsdouble d;
    JS_ValueToNumber(cx, argv[0], &d);
    const int type = (int)d;

    *ret = JSVAL_FALSE;
    const TYPE_ARRAY &types = p->getTypes();
    for (TYPE_ARRAY::const_iterator i = types.begin(); i != types.end(); ++i) {
        if ((*i)->getTypeValue() == type) {
            *ret = JSVAL_TRUE;
            break;
        }
    }

    return JS_TRUE;
}

/**
 * pokemon.isImmune(move)
 */
JSBool isImmune(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_OBJECT(v)) {
        return JS_FALSE;
    }
    MoveObject move(JSVAL_TO_OBJECT(v));
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    // todo: review this (in case effectiveness transformer is needed)
    const TYPE_ARRAY &types = p->getTypes();
    ScriptContext *scx = (ScriptContext *)JS_GetContextPrivate(cx);
    const PokemonType *moveType = move.getType(scx);
    for (TYPE_ARRAY::const_iterator i = types.begin(); i != types.end(); ++i) {
        const double factor = moveType->getMultiplier(**i);
        if (factor == 0.00) {
            *ret = JSVAL_TRUE;
            break;
        }
    }
    *ret = JSVAL_FALSE;
    return JS_TRUE;
}

JSBool isMoveUsed(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_INT(v)) {
        return JS_FALSE;
    }
    const int slot = JSVAL_TO_INT(v);

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    *ret = BOOLEAN_TO_JSVAL(p->isMoveUsed(slot));

    return JS_TRUE;
}

JSBool getStatLevel(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    double d;
    JS_ValueToNumber(cx, v, &d);
    const STAT stat = (STAT)(int)d;

    assert(stat <= S_EVASION);

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    *ret = INT_TO_JSVAL(p->getStatLevel(stat));
    return JS_TRUE;
}

JSBool setStatLevel(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    double d;
    JS_ValueToNumber(cx, argv[0], &d);
    const STAT stat = (STAT)(int)d;

    JS_ValueToNumber(cx, argv[1], &d);
    const int level = (int)d;

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    p->setStatLevel(stat, level);
    return JS_TRUE;
}

JSBool getStat(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_INT(v)) {
        return JS_FALSE;
    }
    const int stat = JSVAL_TO_INT(v);

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    *ret = INT_TO_JSVAL(p->getStat(STAT(stat)));

    return JS_TRUE;
}

JSBool getMove(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_INT(v)) {
        return JS_FALSE;
    }
    const int slot = JSVAL_TO_INT(v);

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    MoveObject *sobj = p->getMove(slot);
    if (sobj) {
        *ret = OBJECT_TO_JSVAL(sobj->getObject());
    } else {
        *ret = JSVAL_NULL;
    }

    return JS_TRUE;
}

JSBool getStatus(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_STRING(v)) {
        return JS_FALSE;
    }
    char *str = JS_GetStringBytes(JSVAL_TO_STRING(v));

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    StatusObject *sobj = p->getStatus(str);
    if (sobj) {
        *ret = OBJECT_TO_JSVAL(sobj->getObject());
    } else {
        *ret = JSVAL_NULL;
    }

    return JS_TRUE;
}

JSBool sendMessage(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_STRING(v)) {
        return JS_FALSE;
    }
    char *str = JS_GetStringBytes(JSVAL_TO_STRING(v));

    const int c = argc - 1;
    ScriptValue val[c];
    for (int i = 0; i < c; ++i) {
        val[i] = ScriptValue::fromValue((void *)argv[i + 1]);
    }

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    ScriptValue vret = p->sendMessage(str, c, val);
    if (vret.failed()) {
        *ret = JSVAL_NULL;
    } else {
        *ret = (jsval)vret.getValue();
    }
    return JS_TRUE;
}

JSBool hasAbility(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    jsval v = argv[0];
    if (!JSVAL_IS_STRING(v)) {
        return JS_FALSE;
    }
    char *str = JS_GetStringBytes(JSVAL_TO_STRING(v));

    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    *ret = BOOLEAN_TO_JSVAL(p->hasAbility(str));

    return JS_TRUE;
}

/**
 * pokemon.execute(move, target, inform)
 */
JSBool execute(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    bool inform = false;
    if (argv[2] != JSVAL_VOID) {
        assert(JSVAL_IS_BOOLEAN(argv[2]));
        inform = JSVAL_TO_BOOLEAN(argv[2]);
    }
    assert(JSVAL_IS_OBJECT(argv[0]));
    MoveObject move(JSVAL_TO_OBJECT(argv[0]));
    Pokemon *target = NULL;
    if (argv[1] != JSVAL_VOID) {
        assert(JSVAL_IS_OBJECT(argv[1]));
        JSObject *objp = JSVAL_TO_OBJECT(argv[1]);
        target = (Pokemon *)JS_GetPrivate(cx, objp);
    }
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    p->executeMove(&move, target, inform);
    return JS_TRUE;
}

JSBool PokemonSet(JSContext *cx, JSObject *obj, jsval id, jsval *vp) {
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    int tid = JSVAL_TO_INT(id);
    switch (tid) {
        case PTI_HP: {
            jsdouble d;
            JS_ValueToNumber(cx, *vp, &d);
            p->setHp(ceil(d));
        } break;
    }
    return JS_TRUE;
}

JSBool toString(JSContext *cx,
        JSObject *obj, uintN argc, jsval *argv, jsval *ret) {
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);

    stringstream ss;
    ss << "$p{" << p->getParty() << "," << p->getPosition() << "}";
    const string str = ss.str();
    char *pstr = JS_strdup(cx, str.c_str());
    JSString *ostr = JS_NewString(cx, pstr, str.length());
    *ret = STRING_TO_JSVAL(ostr);

    return JS_TRUE;
}

JSBool pokemonGet(JSContext *cx, JSObject *obj, jsval id, jsval *vp) {
    Pokemon *p = (Pokemon *)JS_GetPrivate(cx, obj);
    int tid = JSVAL_TO_INT(id);
    switch (tid) {
        case PTI_SPECIES: {
            string name = p->getSpeciesName();
            char *pstr = JS_strdup(cx, name.c_str());
            JSString *str = JS_NewString(cx, pstr, name.length());
            *vp = STRING_TO_JSVAL(str);
        } break;

        case PTI_NAME: {
            string name = p->getName();
            char *pstr = JS_strdup(cx, name.c_str());
            JSString *str = JS_NewString(cx, pstr, name.length());
            *vp = STRING_TO_JSVAL(str);
        } break;

        case PTI_BASE: {
            jsval arr[STAT_COUNT];
            for (int i = 0; i < STAT_COUNT; ++i) {
                arr[i] = INT_TO_JSVAL(p->getBaseStat((STAT)i));
            }
            JSObject *objArr = JS_NewArrayObject(cx, STAT_COUNT, arr);
            *vp = OBJECT_TO_JSVAL(objArr);
        } break;

        case PTI_IV: {
            jsval arr[STAT_COUNT];
            for (int i = 0; i < STAT_COUNT; ++i) {
                arr[i] = INT_TO_JSVAL(p->getIv((STAT)i));
            }
            JSObject *objArr = JS_NewArrayObject(cx, STAT_COUNT, arr);
            *vp = OBJECT_TO_JSVAL(objArr);
        } break;

        case PTI_EV: {
            jsval arr[STAT_COUNT];
            for (int i = 0; i < STAT_COUNT; ++i) {
                arr[i] = INT_TO_JSVAL(p->getEv((STAT)i));
            }
            JSObject *objArr = JS_NewArrayObject(cx, STAT_COUNT, arr);
            *vp = OBJECT_TO_JSVAL(objArr);
        } break;

        case PTI_STAT: {
            jsval arr[STAT_COUNT];
            for (int i = 0; i < STAT_COUNT; ++i) {
                arr[i] = INT_TO_JSVAL(p->getRawStat((STAT)i));
            }
            JSObject *objArr = JS_NewArrayObject(cx, STAT_COUNT, arr);
            *vp = OBJECT_TO_JSVAL(objArr);
        } break;

        case PTI_LEVEL: {
            *vp = INT_TO_JSVAL(p->getLevel());
        } break;

        case PTI_GENDER: {
            *vp = INT_TO_JSVAL(p->getGender());
        } break;

        case PTI_HP: {
            *vp = INT_TO_JSVAL(p->getHp());
        } break;

        case PTI_MEMORY: {
            const MoveTemplate *temp = p->getMemory();
            if (temp) {
                ScriptContext *scx = (ScriptContext *)JS_GetContextPrivate(cx);
                MoveObject *move = scx->newMoveObject(temp);
                *vp = OBJECT_TO_JSVAL(move->getObject());
                scx->removeRoot(move);
            } else {
                *vp = JSVAL_NULL;
            }
        } break;
        
        case PTI_FIELD: {
            *vp = OBJECT_TO_JSVAL(p->getField()->getObject()->getObject());
        } break;

        case PTI_PARTY: {
            *vp = INT_TO_JSVAL(p->getParty());
        } break;

        case PTI_POSITION: {
            // todo: maybe change the name of this
            *vp = INT_TO_JSVAL(p->getSlot());
        } break;

        case PTI_MOVE_COUNT: {
            *vp = INT_TO_JSVAL(p->getMoveCount());
        } break;

        case PTI_FAINTED: {
            *vp = BOOLEAN_TO_JSVAL(p->isFainted());
        } break;

        case PTI_MASS: {
            JS_NewNumberValue(cx, p->getMass(), vp);
        } break;
    }
    return JS_TRUE;
}

JSPropertySpec pokemonProperties[] = {
    { "addStatus", 0, JSPROP_PERMANENT, NULL, NULL },
    { "species", PTI_SPECIES, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "name", PTI_NAME, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "base", PTI_BASE, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "iv", PTI_IV, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "ev", PTI_EV, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "stat", PTI_STAT, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "level", PTI_LEVEL, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "nature", PTI_NATURE, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "hp", PTI_HP, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, PokemonSet },
    { "types", PTI_TYPES, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "gender", PTI_GENDER, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "memory", PTI_MEMORY, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "field", PTI_FIELD, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "party", PTI_PARTY, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "position", PTI_POSITION, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "moveCount", PTI_MOVE_COUNT, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "fainted", PTI_FAINTED, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { "mass", PTI_MASS, JSPROP_PERMANENT | JSPROP_SHARED, pokemonGet, NULL },
    { 0, 0, 0, 0, 0 }
};

JSFunctionSpec pokemonFunctions[] = {
    JS_FS("applyStatus", applyStatus, 2, 0, 0),
    JS_FS("execute", execute, 3, 0, 0),
    JS_FS("hasAbility", hasAbility, 1, 0, 0),
    JS_FS("removeStatus", removeStatus, 1, 0, 0),
    JS_FS("isImmune", isImmune, 1, 0, 0),
    JS_FS("getStatus", getStatus, 1, 0, 0),
    JS_FS("getMove", getMove, 1, 0, 0),
    JS_FS("isMoveUsed", isMoveUsed, 1, 0, 0),
    JS_FS("popRecentDamage", popRecentDamage, 0, 0, 0),
    JS_FS("getStatLevel", getStatLevel, 1, 0, 0),
    JS_FS("setStatLevel", setStatLevel, 2, 0, 0),
    JS_FS("toString", toString, 0, 0, 0),
    JS_FS("sendMessage", sendMessage, 1, 0, 0),
    JS_FS("getStat", getStat, 1, 0, 0),
    JS_FS("isType", isType, 1, 0, 0),
    JS_FS_END
};

} // anonymous namespace

PokemonObject *ScriptContext::newPokemonObject(Pokemon *p) {
    JSContext *cx = (JSContext *)m_p;
    JS_BeginRequest(cx);
    JSObject *obj = JS_NewObject(cx, NULL, NULL, NULL);
    PokemonObject *ret = new PokemonObject(obj);
    addRoot(ret);
    JS_DefineProperties(cx, obj, pokemonProperties);
    JS_DefineFunctions(cx, obj, pokemonFunctions);
    JS_SetPrivate(cx, obj, p);
    JS_EndRequest(cx);
    return ret;
}

}

