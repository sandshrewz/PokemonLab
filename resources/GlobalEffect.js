/*
 * File:   GlobalEffect.js
 * Author: Catherine
 *
 * Created on April 13 2009, 2:30 AM
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

makeEffect(StatusEffect, {
    id : "GlobalEffect",
    radius: StatusEffect.RADIUS_GLOBAL,
    ticked_ : false,
    beginTick : function() {
        this.ticked_ = false;
    },
    tick : function() {
        if (!this.ticked_) {
            this.ticked_ = true;
            if (this.tickField(this.subject.field)) {
                return;
            }
        }
        this.tickPokemon();
    },
    endTick : function(field) {
        if (!this.ticked_) {
            this.tickField(field);
        }
    },
    tickField : function() { },
    tickPokemon : function() { }
});

makeEffect(GlobalEffect, {
    id : "WeatherEffect",
    turns_ : 5,
    applyEffect : function() {
        this.tier = 3 + 0.1 * this.idx_;
        return true;
    },
    tickField : function(field) {
        if ((this.turns_ != -1) && (--this.turns_ == 0)) {
            var effect = getGlobalController(field);
            effect.removeGlobalEffect(field, this.idx_);
            return true;
        }
        this.tickWeather(field);
        return false;
    },
    informApplied : function(user) {
        user.field.print(this.text_(1));
        var turns = user.sendMessage("informApplyWeather", this.idx_);
        if (turns) {
            this.turns_ = turns;
        }
    },
    tickWeather : function(field) {
        field.print(this.text_(2));
    },
    informFinished : function(field) {
        field.print(this.text_(4));
    },
    tickPokemon : function() {
        if (this.subject.field.sendMessage("informWeatherEffects"))
            return;
        var subject = this.subject;
        var flags = getGlobalController(subject.field).flags;
        if (subject.sendMessage("informWeatherHealing", flags))
            return;
        if (flags[GlobalEffect.SAND] ||
                flags[GlobalEffect.HAIL] ||
                        (flags[GlobalEffect.SUN]
                        && subject.sendMessage("informSunDamage"))) {
            var denominator = 16;
            var field = subject.field;
            if (flags[GlobalEffect.SUN]) {
                // damaged by ability
                field.print(Text.weather_sun(3, subject, subject.ability));
                if (subject.sendMessage("informSunDamage")) {
                    denominator = 8;
                }
            } else if (flags[GlobalEffect.SAND]) {
                if (subject.sendMessage("informSandDamage"))
                    return;
                if (subject.isType(Type.GROUND) ||
                        subject.isType(Type.ROCK) ||
                        subject.isType(Type.STEEL))
                    return;
                field.print(Text.weather_sandstorm(3, subject));
            } else {
                if (subject.sendMessage("informHailDamage"))
                    return;
                if (subject.isType(Type.ICE))
                    return;
                field.print(Text.weather_hail(3, subject));
            }

            var damage = Math.floor(subject.getStat(Stat.HP) / denominator);
            if (damage < 1) damage = 1;
            subject.hp -= damage;
        }
    }
});

GlobalEffect.EFFECTS = ["RainEffect", "SandEffect", "SunEffect", "HailEffect",
                        "FogEffect", "UproarEffect", "GravityEffect",
                        "TrickRoomEffect"];

GlobalEffect.RAIN = 0;
GlobalEffect.SAND = 1;
GlobalEffect.SUN = 2;
GlobalEffect.HAIL = 3;
GlobalEffect.FOG = 4;
GlobalEffect.UPROAR = 5;
GlobalEffect.GRAVITY = 6;
GlobalEffect.TRICK_ROOM = 7;

makeEffect(WeatherEffect, {
    id : "RainEffect",
    name : Text.weather_rain(0),
    text_ : Text.weather_rain,
    idx_ : GlobalEffect.RAIN,
    tickPokemon : function() {
        if (!this.subject.field.sendMessage("informWeatherEffects")) {
            this.subject.sendMessage("informRainHealing");
        }
        WeatherEffect.prototype.tickPokemon.call(this);
    },
    // @mod 1, 3, Rain
    modifier : function(field, user, target, move, critical) {
        if (field.sendMessage("informWeatherEffects"))
            return null;
        var type = move.type;
        if (type == Type.FIRE)
            return [1, 0.5, 3];
        if (type == Type.WATER)
            return [1, 1.5, 3];
        return null;
    }
});

makeEffect(WeatherEffect, {
    id : "SandEffect",
    name : Text.weather_sandstorm(0),
    text_ : Text.weather_sandstorm,
    idx_ : GlobalEffect.SAND,
    // @stat SPDEFENCE, 3, Sand
    statModifier : function(field, stat, subject) {
        if (field.sendMessage("informWeatherEffects"))
            return null;
        if (stat != Stat.SPDEFENCE)
            return null;
        if (!subject.isType(Type.ROCK))
            return null;
        return [1.5, 3];
    }
});

makeEffect(WeatherEffect, {
    id : "SunEffect",
    name : Text.weather_sun(0),
    text_ : Text.weather_sun,
    idx_ : GlobalEffect.SUN,
    // @mod 1, 4, Sun
    modifier : function(field, user, target, move, critical) {
        if (field.sendMessage("informWeatherEffects"))
            return null;
        var type = move.type;
        if (type == Type.FIRE)
            return [1, 1.5, 4];
        if (type == Type.WATER)
            return [1, 0.5, 4];
        return null;
    }
});

makeEffect(WeatherEffect, {
    id : "HailEffect",
    name : Text.weather_hail(0),
    text_ : Text.weather_hail,
    idx_ : GlobalEffect.HAIL,
});

makeEffect(WeatherEffect, {
    id : "FogEffect",
    name : Text.weather_fog(0),
    text_ : Text.weather_fog,
    idx_ : GlobalEffect.FOG,
    informFinished : function() { },
    // @stat ACCURACY, 5, Fog
    statModifier : function(field, stat, subject) {
        if (field.sendMessage("informWeatherEffects"))
            return null;
        if (stat != Stat.ACCURACY)
            return null;
        return [0.6, 5];
    }
});

makeEffect(StatusEffect, {
    id : "UproarEffect",
    name : Text.battle_messages_unique(151),
    idx_ : GlobalEffect.UPROAR,
    users_ : 0,
    informSleepCheck : function(effect) {
        effect.wakeUp();
        return true;
    },
    transformStatus : function(subject, status) {
        if (status.id != "SleepEffect")
            return status;
        if (subject.hasAbility("Soundproof"))
            return status;
        return null;
    },
    informFinished : function() {
        // Does nothing.
    },
    removeUser : function() {
        if (--this.users_ <= 0) {
            this.users_ = 0;
            getGlobalController(this.subject.field).removeGlobalEffect(
                    this.subject.field, GlobalEffect.UPROAR);
        }
    }
});

makeEffect(StatusEffect, {
    id : "TrickRoomEffect",
    name : Text.battle_messages_unique(34),
    idx_ : GlobalEffect.TRICK_ROOM,
    turns_ : 5,
    informSpeedSort : function() {
        // Sort speeds in ascending order.
        return false;
    },
    informFinished : function(field) {
        field.print(Text.battle_messages_unique(25));
    },
    endTick : function(field) {
        if (--this.turns_ == 0) {
            getGlobalController(field).removeGlobalEffect(
                    field, GlobalEffect.TRICK_ROOM);
        }
    }
});

makeEffect(StatusEffect, {
    id : "GravityEffect",
    name : Text.battle_messages_unique(112),
    idx_ : GlobalEffect.GRAVITY,
    turns_ : 5,
    forbidden_ : ["Fly", "Bounce", "Magnet Rise", "Hi Jump Kick",
                  "Splash", "Jump Kick"],
    applyEffect : function() {
        var effect = this.subject.getStatus("MagnetRiseEffect");
        if (effect) {
            this.subject.removeStatus(effect);
        }
        effect = this.subject.getStatus("ChargeMoveEffect");
        if (effect && (this.forbidden_.indexOf(effect.move.name) != -1)) {
            this.subject.removeStatus(effect);
        }
        return true;
    },
    vulnerability : function(user, target) {
        return Type.GROUND;
    },
    // @stat ACCURACY, 12, Gravity
    statModifier : function(field, stat, subject, target) {
        if (stat != Stat.ACCURACY)
            return null;
        return [1.6, 12];
    },
    vetoSelection : function(user, move) {
        return (this.forbidden_.indexOf(move.name) != -1);
    },
    vetoExecution : function(field, user, target, move) {
        if (target != null)
            return false;
        if (this.forbidden_.indexOf(move.name) == -1)
            return false;
        field.print(Text.battle_messages_unique(114, user, move));
        return true;
    },
    informFinished : function(field) {
        field.print(Text.battle_messages_unique(115));
    },
    endTick : function(field) {
        if (--this.turns_ == 0) {
            getGlobalController(field).removeGlobalEffect(
                    field, GlobalEffect.GRAVITY);
        }
    }
});

makeEffect(StatusEffect, {
    id : "GlobalEffectController",
    ctor : function() {
        this.flags = [false, false, false, false, false, false, false, false];
    },
    removeGlobalEffect : function(field, idx) {
        if (!this.flags[idx])
            return;
        this.flags[idx] = false;
        var effect = field.getStatus(GlobalEffect.EFFECTS[idx]);
        if (effect) {
            effect.informFinished(field);
            field.removeStatus(effect);
        }
    },
    applyGlobalEffect : function(user, idx) {
        this.flags[idx] = true;
        var f = getGlobalEffect(idx);
        var effect = user.field.applyStatus(new f());
        return effect;
    },
    applyWeather : function(user, idx) {
        var fail = [];
        for (var i = GlobalEffect.RAIN; i <= GlobalEffect.FOG; ++i) {
            if (this.flags[i]) {
                fail.push(i);
            }
        }
        if (fail.length > 1) {
            fail.pop();
        }
        if (fail.indexOf(idx) != -1) {
            return null;
        }
        for (var i = GlobalEffect.RAIN; i <= GlobalEffect.FOG; ++i) {
            this.removeGlobalEffect(user.field, i);
        }
        var effect = this.applyGlobalEffect(user, idx);
        effect.informApplied(user);
        return effect;
    },
    simulateBufferOverflow : function() {
        var idx = this.flags.indexOf(true);
        if (idx == -1)
            return;
        for (var i = 0; i < idx; ++i) {
            var effect = this.applyGlobalEffect(this.subject, i);
            effect.turns_ = -1; // indefinite duration
        }
    },
    getFlags : function() {
        if (!this.subject.field.sendMessage("informWeatherEffects"))
            return this.flags;
        var flags = this.flags.concat();
        for (var i = GlobalEffect.RAIN; i <= GlobalEffect.FOG; ++i) {
            flags[i] = false;
        }
        return flags;
    }
});

function getGlobalEffect(idx) {
    return this[GlobalEffect.EFFECTS[idx]];
}

function getGlobalController(field) {
    var effect = field.getStatus("GlobalEffectController");
    if (effect) {
        return effect;
    }
    return field.applyStatus(new GlobalEffectController());
}

function isWeatherActive(user, idx) {
    if (user.field.sendMessage("informWeatherEffects"))
        return false;
    return getGlobalController(user.field).flags[idx];
}
