/*
 * File:   items.js
 * Author: Catherine
 *
 * Created on June 2, 2009, 12:18 AM
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

function HoldItem(name) {
    this.name = this.id = name;
    HoldItem[name] = this;
}

HoldItem.prototype = new StatusEffect();
HoldItem.prototype.type = StatusEffect.TYPE_ITEM;
HoldItem.prototype.switchOut = function() {
    return false;
};
HoldItem.prototype.use = function() { };
HoldItem.prototype.consume = function() {
    if (this.subject.fainted)
        return;
    var effect = this.subject.getStatus("ItemConsumedEffect");
    if (!effect) {
        effect = new StatusEffect("ItemConsumedEffect");
        var party_ = this.subject.party;
        var position_ = this.subject.position;
        effect.applyEffect = function() {
            if (this.subject.party != party_)
                return false;
            if (this.subject.position != position_)
                return false;
            return true;
        };
        effect = this.subject.field.applyStatus(effect);
    }
    effect.item_ = this.id;
    this.subject.removeStatus(this);
};
HoldItem.prototype.unapplyEffect = function() {
    if (this.subject.fainted)
        return;
    this.subject.sendMessage("informLostItem");
};
HoldItem.prototype.getState = function() {
    if (!this.subject || (this.state != StatusEffect.STATE_ACTIVE))
        return this.state;
    if (this.subject.getStatus("EmbargoEffect")
            || this.subject.hasAbility("Klutz"))
        return StatusEffect.STATE_DEACTIVATED;
    return StatusEffect.STATE_ACTIVE;
};
HoldItem.prototype.condition = function() {
    return false;
};
HoldItem.prototype.checkCondition_ = function() {
    if (!this.condition())
        return false;
    this.use(this.subject);
    return true;
};
HoldItem.prototype.informFinishedExecution = function(subject, move) {
    if (this.checkCondition_())
        return;
    StatusEffect.prototype.informFinishedExecution.call(this, subject, move);
};
HoldItem.prototype.tier = 10;
HoldItem.prototype.tick = HoldItem.prototype.checkCondition_;

function makeItem(obj) {
    var item = new HoldItem(obj.name);
    for (var p in obj) {
        if (obj[p]) {
            item[p] = obj[p];
        } else {
            delete item[p];
        }
    }
}

function makeEvadeItem(item) {
    makeItem({
        name : item,
        // @stat ACCURACY, 8, Evade Items
        statModifier : function(field, stat, subject, target) {
            if (stat != Stat.ACCURACY)
                return null;
            if (target != this.subject)
                return null;
            return [0.9, 8];
        }
    });
}

function makeStatusCureItem(item, ids) {
    makeItem({
        name : item,
        berry_ : true,
        use : function(user) {
            ids.forEach(function(id) {
                var effect = user.getStatus(id);
                if (effect) {
                    user.field.print(Text.item_messages(1, user, this, effect));
                    user.removeStatus(effect);
                }
            }, this);
            this.consume();
        },
        condition : function() {
            for (var i in ids) {
                var id = ids[i];
                if (this.subject.getStatus(id)) {
                    return true;
                }
            }
            return false;
        }
    });
}

function makePinchBerry(item, effect) {
    makeItem({
        name : item,
        berry_ : true,
        use : effect,
        condition : function() {
            var d = this.subject.sendMessage("informPinchBerry");
            if (!d) d = 4;
            var threshold = Math.floor(this.subject.getStat(Stat.HP) / d);
            return (this.subject.hp <= threshold);
        }
    });
}

function makeFlavourHealingBerry(item, stat) {
    makeItem({
        name : item,
        berry_ : true,
        condition : function() {
            var threshold = Math.floor(this.subject.getStat(Stat.HP) / 2);
            return (this.subject.hp <= threshold);
        },
        use : function(user) {
            user.field.print(Text.item_messages(0, user, this));
            var delta = Math.floor(user.getStat(Stat.HP) / 8);
            user.hp += delta;
            if (user.getNatureEffect(stat) < 1.0) {
                user.field.print(Text.item_messages(4, user));
                user.applyStatus(user, new ConfusionEffect());
            }
            this.consume();
        }
    })
}

function makeHealingBerry(item, func) {
    makeItem({
        name: item,
        berry_: true,
        condition : function() {
            var threshold = Math.floor(this.subject.getStat(Stat.HP) / 2);
            return (this.subject.hp <= threshold);
        },
        use : function(user) {
            user.field.print(Text.item_messages(0, user, this));
            user.hp += func(user);
            this.consume();
        }
    });
}

function makeStatBoostBerry(item, stat) {
    makePinchBerry(item, function(user) {
        if (user.getStatLevel(stat) == 6)
            return;
        var effect = new StatChangeEffect(stat, 1);
        effect.silent = true;
        if (!user.applyStatus(user, effect))
            return;
        user.field.print(Text.item_messages(3, user, this,
                Text.stats_long(stat)));
        this.consume();
    });
}

function makeTypeResistingBerry(item, type) {
    makeItem({
        name : item,
        berry_ : true,
        modifier : function(field, user, target, move, critical) {
            if (target != this.subject)
                return null;
            if (type != move.type)
                return null;
            if (move.power <= 1)
                return null;
            var effectiveness = field.getEffectiveness(move.type, target);
            if ((effectiveness < 2.0) && (type != Type.NORMAL))
                return null;
            field.print(Text.item_messages(5, target, item, move));
            this.consume();
            return [2, 0.5, 3];
        }
    });
}

function makeFeedbackDamageBerry(item, moveclass) {
    makeItem({
        name : item,
        berry_ : true,
        informDamaged : function(user, move, damage) {
            // Damage feedback berries don't trigger if the user has fainted,
            // but we can't use this.subject.fainted because faint() isn't
            // called until after the informDamaged message is processed.
            if (this.subject.hp <= 0)
                return;
            if (this.subject == user)
                return;
            if (move.moveClass != moveclass)
                return;
            user.field.print(Text.item_messages(6, user, this.subject, this));
            user.hp -= Math.floor(user.getStat(Stat.HP) / 8);
            this.consume();
        }
    });
}

function makeTypeBoostingItem(item, type) {
    makeItem({
        name : item,
        type_ : type,
        // @mod 0, 1, Type Boosting Items
        modifier : function(field, user, target, move, critical) {
            if (user != this.subject)
                return null;
            if (move.type != type)
                return null;
            return [0, 1.2, 1];
        }
    });
}

function makePlateItem(item, type) {
    makeTypeBoostingItem(item, type);
    HoldItem[item].plate_ = true;
}

function makeChoiceItem(item, func) {
    makeItem({
        name : item,
        choiceItem_ : true,
        statModifier : func,
        informFinishedSubjectExecution : function() {
            var move_ = this.subject.lastMove;
            if (!move_ || (move_.name == "Mimic") || (move_.name == "Sketch")
                    || (move_.name == "Transform")
                    || (this.subject.getPp(move_) == -1)
                    || this.subject.getStatus("ChoiceLockEffect"))
                return;
            var effect = new StatusEffect("ChoiceLockEffect");
            effect.vetoSelection = function(user, move) {
                if (user != this.subject)
                    return false;
                var item_ = user.item;
                if (!item_ || !item_.choiceItem_
                        || (item_.getState() != StatusEffect.STATE_ACTIVE)) {
                    this.subject.removeStatus(this);
                    return false;
                }
                return (move.name != move_.name);
            };
            this.subject.applyStatus(this.subject, effect);
        }
    });
}


/**
 * Item that boost a stat for a certain group of species
 * species is a list of species
 * modifiers is a dict of stat->multiplier
 */
function makeSpeciesBoostingItem(item, species, modifiers) {
    makeItem({
        name: item,
        statModifier: function(field, stat, subject) {
            if (subject != this.subject)
                return null;
            var found = false;
            for (var i in species) {
                if (species[i] ==  subject.species) {
                    found = true;
                    break;
                }
            }
            if (!found)
                return null;
            for (var i in modifiers) {
                if (modifiers[i][0] == stat) {
                    return [modifiers[i][1], modifiers[i][2]];
                }
            }
            return null;
        }
    });
}

function makeStatusInducingItem(item, effect) {
    makeItem({
        name: item,
        tier: 6,
        subtier: 6,
        tick: function() {
            this.subject.applyStatus(this.subject, effect);
        }
    });
}

function makeStabBoostItem(item, species) {
    makeItem({
        name: item,
        // @mod 0, 2, STAB Boosting Items
        modifier: function(field, user, target, move, critical) {
            if (user != this.subject)
                return null;
            if (user.species != species)
                return null;
            if (!(user.isType(move.type)))
                return null;
            return [0, 1.2, 2];
        }
    });
}

function makeMoveClassBoostingItem(item, class_) {
    makeItem({
        name : item,
        // @mod 0, 2, Move Class Boosting Items
        modifier : function(field, user, target, move, critical) {
            if (user != this.subject)
                return null;
            if (move.moveClass != class_)
                return null;
            return [0, 1.1, 2];
        }
    });
}

function makeWeatherProlongItem(item, weather) {
    makeItem({
        name : item,
        informApplyWeather : function(idx) {
            if (idx == weather) {
                return 8;
            }
            return false;
        }
    });
}

// @stat ATTACK, 3, Choice Band
makeChoiceItem("Choice Band", function(field, stat, subject) {
    if (subject != this.subject)
        return null;
    if (stat != Stat.ATTACK)
        return null;
    return [1.5, 3];
});

// @stat SPATTACK, 3, Choice Specs
makeChoiceItem("Choice Specs", function(field, stat, subject) {
    if (subject != this.subject)
        return null;
    if (stat != Stat.SPATTACK)
        return null;
    return [1.5, 3];
});

// @stat SPEED, 4, Choice Scarf
makeChoiceItem("Choice Scarf", function(field, stat, subject) {
    if (subject != this.subject)
        return null;
    if (stat != Stat.SPEED)
        return null;
    return [1.5, 4];
});

makeItem({
    name : "Life Orb",
    // @mod 2, 0, Life Orb
    modifier : function(field, user, target, move, critical, targets) {
        if (user != this.subject)
            return null;
        if (move.name == "__confusion")
            return null;
        if (move.delayedAttack_)
            return null;
        return [2, 1.3, 0];
    },
    informDamaging : function(move, target) {
        if (target == this.subject)
            return;
        if (move.moveClass == MoveClass.OTHER)
            return;
        this.recoil_ = true;
    },
    informFinishedSubjectExecution : function(move) {
        var subject = this.subject;
        if (this.recoil_) {
            var recoil = Math.floor(subject.getStat(Stat.HP) / 10);
            if (recoil < 1) recoil = 1;
            subject.hp -= recoil;
        }
        this.recoil_ = false;
    }
});

makeItem({
    name : "Quick Claw",
    value_ : -1,
    switchIn : function() {
        this.value_ = -1;
    },
    switchOut : function() {
        this.value_ = -1;
        return false;
    },
    applyEffect : function() {
        this.value_ = -1;
        return true;
    },
    inherentPriority : function() {
        if (this.value_ == -1) {
            this.value_ = this.subject.field.random(0.2) ? 3 : 0;
        }
        return this.value_;
    },
    informBeginExecution : function() {
        if (isMovingLast(this.subject.field, this.subject)) {
            return;
        }
        if (this.value_ > 0) {
            this.subject.field.print(Text.item_messages(2, this.subject));
        }
    },
    informFinishedSubjectExecution : function() {
        this.value_ = -1;
    }
});

makeStatBoostBerry("Liechi Berry", Stat.ATTACK);
makeStatBoostBerry("Ganlon Berry", Stat.DEFENCE);
makeStatBoostBerry("Salac Berry", Stat.SPEED);
makeStatBoostBerry("Petaya Berry", Stat.SPATTACK);
makeStatBoostBerry("Apicot Berry", Stat.SPDEFENCE);
makePinchBerry("Starf Berry", function() {
    var stats = [Stat.ATTACK, Stat.DEFENCE, Stat.SPEED, Stat.SPATTACK,
            Stat.SPDEFENCE];
    var field = this.subject.field;
    var stat = stats[field.random(0, stats.length - 1)];
    if (this.subject.getStatLevel(stat) == 6)
        return;
    var effect = new StatChangeEffect(stat, 2);
    effect.silent = true;
    if (!this.subject.applyStatus(this.subject, effect))
        return;
    field.print(Text.item_messages(3, this.subject, this,
            Text.stats_long(stat)));
    this.consume();
});

makeFlavourHealingBerry("Figy Berry", Stat.ATTACK);
makeFlavourHealingBerry("Wiki Berry", Stat.SPATTACK);
makeFlavourHealingBerry("Mago Berry", Stat.SPEED);
makeFlavourHealingBerry("Aguav Berry", Stat.SPDEFENCE);
makeFlavourHealingBerry("Iapapa Berry", Stat.DEFENCE);

makeHealingBerry("Oran Berry", function(p) { return 10; });
makeHealingBerry("Berry Juice", function(p) { return 20; });
makeHealingBerry("Sitrus Berry", function(p) {
    return Math.floor(p.getStat(Stat.HP) / 4);
});

makeFeedbackDamageBerry("Jaboca Berry", MoveClass.PHYSICAL);
makeFeedbackDamageBerry("Rowap Berry", MoveClass.SPECIAL);

makeItem({
    name : "Enigma Berry",
    berry_ : true,
    informDamaged : function(user, move, damage) {
        var subject = this.subject;
        if (user == subject)
            return;
        var field = subject.field;
        if (field.getEffectiveness(move.type, subject) < 2.0)
            return;
        if (subject.fainted)
            return;
        field.print(Text.item_messages(0, subject, this));
        subject.hp += Math.floor(subject.getStat(Stat.HP) / 4);
        this.consume();
    }
});

makeItem({
    name : "Custap Berry",
    berry_ : true,
    applyCustap : function() {
        var subject = this.subject;
        var threshold = Math.floor(this.subject.getStat(Stat.HP) / 4);
        if (this.subject.hp > threshold)
            return;
        if (subject.getStatus("CustapEffect"))
            return;

        var effect = new StatusEffect("CustapEffect");
        effect.inherentPriority = function() {
            this.used_ = true;
            return 3;
        };
        effect.switchOut = function() {
            return true;
        };
        effect.informBeginExecution = function() {
            if (!this.used_) return;
            var subject = this.subject;

            // Check if Pursuit succeeded on a fleeing opponent
            var pursuit = subject.getStatus("PursuitEffect");
            if (pursuit && pursuit.executed) {
                return;
            }

            if (isMovingLast(subject.field, subject)) return;

            // TODO: Language file
            var item = (subject.item != null) ? subject.item : "None";
            subject.field.print(Text.item_messages(11, subject, item));
            subject.removeStatus(this);

            if (subject.item != null) {
                subject.item.consume();
            }
        }
        subject.applyStatus(subject, effect);
    },
    switchIn : function() {
        this.applyCustap();
    },
    applyEffect : function() {
        this.applyCustap();
        return true;
    },
    informDamaged : function(user, move, damage) {
        this.applyCustap();
    }
});

makeTypeResistingBerry("Babiri Berry", Type.STEEL);
makeTypeResistingBerry("Charti Berry", Type.ROCK);
makeTypeResistingBerry("Chilan Berry", Type.NORMAL);
makeTypeResistingBerry("Chople Berry", Type.FIGHTING);
makeTypeResistingBerry("Coba Berry", Type.FLYING);
makeTypeResistingBerry("Colbur Berry", Type.DARK);
makeTypeResistingBerry("Haban Berry", Type.DRAGON);
makeTypeResistingBerry("Kasib Berry", Type.GHOST);
makeTypeResistingBerry("Kebia Berry", Type.POISON);
makeTypeResistingBerry("Occa Berry", Type.FIRE);
makeTypeResistingBerry("Passho Berry", Type.WATER);
makeTypeResistingBerry("Payapa Berry", Type.PSYCHIC);
makeTypeResistingBerry("Rindo Berry", Type.GRASS);
makeTypeResistingBerry("Shuca Berry", Type.GROUND);
makeTypeResistingBerry("Tanga Berry", Type.BUG);
makeTypeResistingBerry("Wacan Berry", Type.ELECTRIC);
makeTypeResistingBerry("Yache Berry", Type.ICE);

makeTypeBoostingItem("SilverPowder", Type.BUG);
makeTypeBoostingItem("Metal Coat", Type.STEEL);
makeTypeBoostingItem("Soft Sand", Type.GROUND);
makeTypeBoostingItem("Hard Stone", Type.ROCK);
makeTypeBoostingItem("Miracle Seed", Type.GRASS);
makeTypeBoostingItem("BlackGlasses", Type.DARK);
makeTypeBoostingItem("Black Belt", Type.FIGHTING);
makeTypeBoostingItem("Magnet", Type.ELECTRIC);
makeTypeBoostingItem("Mystic Water", Type.WATER);
makeTypeBoostingItem("Sharp Beak", Type.FLYING);
makeTypeBoostingItem("Poison Barb", Type.POISON);
makeTypeBoostingItem("NeverMeltIce", Type.ICE);
makeTypeBoostingItem("Spell Tag", Type.GHOST);
makeTypeBoostingItem("TwistedSpoon", Type.PSYCHIC);
makeTypeBoostingItem("Charcoal", Type.FIRE);
makeTypeBoostingItem("Dragon Fang", Type.DRAGON);
makeTypeBoostingItem("Silk Scarf", Type.NORMAL);

makePlateItem("Flame Plate", Type.FIRE);
makePlateItem("Splash Plate", Type.WATER);
makePlateItem("Zap Plate", Type.ELECTRIC);
makePlateItem("Meadow Plate", Type.GRASS);
makePlateItem("Icicle Plate", Type.ICE);
makePlateItem("Fist Plate", Type.FIGHTING);
makePlateItem("Toxic Plate", Type.POISON);
makePlateItem("Earth Plate", Type.GROUND);
makePlateItem("Sky Plate", Type.FLYING);
makePlateItem("Mind Plate", Type.PSYCHIC);
makePlateItem("Insect Plate", Type.BUG);
makePlateItem("Stone Plate", Type.ROCK);
makePlateItem("Spooky Plate", Type.GHOST);
makePlateItem("Draco Plate", Type.DRAGON);
makePlateItem("Dread Plate", Type.DARK);
makePlateItem("Iron Plate", Type.STEEL);

makeStatusCureItem("Cheri Berry", ["ParalysisEffect"]);
makeStatusCureItem("Chesto Berry", ["SleepEffect"]);
makeStatusCureItem("Pecha Berryy", ["PoisonEffect", "ToxicEffect"]);
makeStatusCureItem("Rawst Berry", ["BurnEffect"]);
makeStatusCureItem("Aspear Berry", ["FreezeEffect"]);
makeStatusCureItem("Persim Berry", ["ConfusionEffect"]);
makeStatusCureItem("Lum Berry", ["ParalysisEffect", "SleepEffect",
        "PoisonEffect", "ToxicEffect", "BurnEffect", "FreezeEffect",
        "ConfusionEffect"]);

makeEvadeItem("Brightpowder");
makeEvadeItem("Lax Incense");

makeWeatherProlongItem("Damp Rock", GlobalEffect.RAIN);
makeWeatherProlongItem("Heat Rock", GlobalEffect.SUN);
makeWeatherProlongItem("Icy Rock", GlobalEffect.HAIL);
makeWeatherProlongItem("Smooth Rock", GlobalEffect.SAND);

// @stat SPATTACK, 3, DeepSeaTooth
makeSpeciesBoostingItem("DeepSeaTooth", ["Clamperl"],
        [[Stat.SPATTACK, 2.0, 3]]);
// @stat SPDEFENCE, 2, DeepSeaScale
makeSpeciesBoostingItem("DeepSeaScale", ["Clamperl"],
        [[Stat.SPDEFENCE, 2.0, 2]]);
// @stat SPATTACK, 3, Light Ball
// @stat ATTACK, 3, Light Ball
makeSpeciesBoostingItem("Light Ball", ["Pikachu"],
        [[Stat.SPATTACK, 2.0, 3],
        [Stat.ATTACK, 2.0, 3]]);
// @stat SPATTACK, 3, Soul Dew
// @stat SPDEFENCE, 2, Soul Dew
makeSpeciesBoostingItem("Soul Dew", ["Latios", "Latias"],
        [[Stat.SPATTACK, 1.5, 3], [Stat.SPDEFENCE, 1.5, 2]]);
// @stat ATTACK, 3, Thick Club
makeSpeciesBoostingItem("Thick Club", ["Cubone", "Marowak"],
        [[Stat.ATTACK, 2.0, 3]]);
// TODO: probably have to change these for transform
// @stat DEFENCE, 3, Metal Powder
// @stat SPDEFENCE, 2, Metal Powder
makeSpeciesBoostingItem("Metal Powder", ["Ditto"], [[Stat.DEFENCE, 2.0, 3],
        [Stat.SPDEFENCE, 2.0, 2]]);
// @stat SPEED, 5, Quick Powder
makeSpeciesBoostingItem("Quick Powder", ["Ditto"], [[Stat.SPEED, 2.0, 5]]);

makeStabBoostItem("Adamant Orb", "Dialga");
makeStabBoostItem("Lustrous Orb", "Palkia");
//todo: griseous orb

makeStatusInducingItem("Flame Orb", new BurnEffect());
makeStatusInducingItem("Toxic Orb", new ToxicEffect());

makeMoveClassBoostingItem("Muscle Band", MoveClass.PHYSICAL);
makeMoveClassBoostingItem("Wise Glasses", MoveClass.SPECIAL);
makeItem({
    name : "Expert Belt",
    // @mod 3, 1, Expert Belt
    modifier : function(field, user, target, move, critical) {
        if (user != this.subject)
            return null;
        if (field.getEffectiveness(move.type, target) < 2.0)
            return null;
        return [3, 1.2, 1];
    }
});

makeItem({
    name : "Light Clay",
    informPartyBuffTurns : function() {
        return 8;
    }
});

makeItem({
    name : "Shed Shell",
    informBlockSwitch : function() {
        return true;
    }
});

makeItem({
    name : "Grip Claw",
    informTemporaryTrapping : function() {
        return 6;
    }
});

makeItem({
    name : "Mosaic Mail",
    informItemSwitch : function() {
        return true;
    }
});

makeItem({
    name : "Leftovers",
    tier : 6,
    subtier : 2,
    tick : function() {
        var max = this.subject.getStat(Stat.HP);
        if (this.subject.hp == max)
            return;
        this.subject.field.print(Text.item_messages(0, this.subject, this));
        var delta = Math.floor(max / 16);
        this.subject.hp += delta;
    }
});

makeItem({
    name : "Black Sludge",
    tier : 6,
    subtier : 2,
    tick : function() {
        var max = this.subject.getStat(Stat.HP);
        if (this.subject.isType(Type.POISON)) {
            if (this.subject.hp == max)
                return;
            this.subject.field.print(Text.item_messages(0, this.subject, this));
            var delta = Math.floor(max / 16);
        } else {
            this.subject.field.print(Text.battle_messages_unique(54,
                    this.subject, this));
            var delta = -Math.floor(max / 8);
        }
        this.subject.hp += delta;
    }
});

function makeNegativePriorityItem(item) {
    makeItem({
        name : item,
        inherentPriority : function() {
            return -2;
        }
    });
}

makeNegativePriorityItem("Full Incense");
makeNegativePriorityItem("Lagging Tail");

makeItem({
    name: "Wide Lens",
    // @stat ACCURACY, 9, Wide Lens
    statModifier: function(field, stat, subject) {
        if (this.subject != subject)
            return null;
        if (stat != Stat.ACCURACY)
            return null;
        return [1.1, 9];
    }
});

makeItem({
    name : "Macho Brace",
    getState : null,
    // @stat SPEED, 3, Macho Brace
    statModifier : function(field, stat, subject) {
        if (subject != this.subject)
            return null;
        if (stat != Stat.SPEED)
            return null;
        return [0.5, 3];
    }
});

makeItem({
    name : "Big Root",
    informAbsorbHealth : function(user, absorb) {
        if (user == this.subject)
            return Math.floor(absorb * 1.3);
        return absorb;
    }
});

makeItem({
    name : "Mental Herb",
    informAttracted : function(inducer) {
        this.subject.field.print(
                    Text.status_effects_attract(4, this.subject, this));
        this.consume();
        return true;
    }
});

makeItem({
    name : "Destiny Knot",
    informAttracted : function(inducer) {
        //todo: message?
        if (!inducer.getStatus("AttractEffect")) {
            inducer.applyStatus(this.subject, new AttractEffect());
        }
        return false;
    }
});

function makeSpeciesCriticalItem(item, species) {
    makeItem({
        name : item,
        criticalModifier : function() { return 2; }
    });
}

makeSpeciesCriticalItem("Lucky Punch", "Chansey");
makeSpeciesCriticalItem("Stick", "Farfetch'd");

function makeCriticalBoostItem(item) {
    makeItem({
        name : item,
        criticalModifier : function() { return 1; }
    });
}

makeCriticalBoostItem("Razor Claw");
makeCriticalBoostItem("Scope Lens");


/*function makeFlinchItem(item) {
    makeItem({
        name : item,
        switchIn : function() {
            var opp = this.subject.field.getActivePokemon(1 - this.subject.party);
            var effect = new StatusEffect("_FlinchItemEffect");
            effect.name = "";
            effect.informDamaged = function(user, move, target) {
                var subject = this.subject;
                if (subject.getStatus("SubstituteEffect") || (damage == 0))
                    return;
                if (move.flags[Flag.FLINCH]) {
                    if (subject.field.random(0.1)) {
                        subject.applyStatus(user, new FlinchEffect());
                    }
                }
            }
            opp.applyStatus(this.subject, effect);
            return true;
        },
        switchOut : function() {
            var opp = this.subject.field.getActivePokemon(1 - this.subject.party);
            opp.removeStatus("_FlinchItemEffect");
            return false;
        }
    });
}

makeFlinchItem("King's Rock");
makeFlinchItem("Razor Fang");

makeItem({
    name : "Shell Bell",
    switchIn : function() {
        var effect = new StatusEffect("_ShellBellEffect");
        effect.name = "Shell Bell";
        effect.informDamaged = function(user, move, damage) {
            if (user == this.subject)
                return;
            if (damage <= 0)
                return;
            if (user.hp == user.getStat(Stat.HP))
                return;
            user.field.print(Text.item_messages(0, user, this));
            user.hp += Math.floor(damage / 8);
        }
        var opp = this.subject.field.getActivePokemon(1 - this.subject.party);
        opp.applyStatus(this.subject, effect);
        return true;
    },
    switchOut : function() {
        var opp = this.subject.field.getActivePokemon(1 - this.subject.party);
        opp.removeStatus("_ShellBellEffect");
        return false;
    }
});*/

makeItem({
    name : "Sticky Barb",
    tier : 6,
    subtier : 18,
    tick : function() {
        var subject = this.subject;
        subject.field.print(Text.item_messages(7, subject, this));
        subject.hp -= Math.floor(subject.getStat(Stat.HP) / 8);
    },
    informDamaged : function(user, move, damage) {
        if (user == this.subject)
            return;
        if (!move.flags[Flag.CONTACT])
            return;
        user.field.print(Text.item_messages(8, user, this));
        user.hp -= Math.floor(user.getStat(Stat.HP) / 8);
        if (user.item)
            return;
        user.field.print(Text.item_messages(9, this, user));
        user.item = this;
        this.subject.item = null;
    }
});

function makeFocusItem(item, consumable, condition) {
    makeItem({
        name : item,
        transformHealthChange : function(delta, user, indirect) {
            var subject = this.subject;
            if (indirect)
                return delta;
            if (delta < subject.hp)
                return delta;
            if (!this.defendMultihit_ && !condition(subject))
                return delta;
            if (this.defendMultihit_ && this.used_)
                return 0;

            this.used_ = true;

            // Shedinja or the final nick of a multihit move
            if (subject.hp == 1) {
                subject.informDamaged(user, subject.field.execution, 0);
            }
            return subject.hp - 1;
        },
        informPartialDamage : function() {
            var maxHp = this.subject.getStat(Stat.HP);
            if (condition(this.subject)) {
                this.defendMultihit_ = true;
            }
        },
        informFinishedExecution: function() {
            if (this.used_) {
                this.subject.field.print(Text.item_messages(10, this.subject,
                    this.name));
                if (consumable) {
                    this.consume();
                }
            }
            this.defendMultihit_ = false;
            this.used_ = false;
        }
    });
}

makeFocusItem("Focus Sash", true, function(subject) {
    var maxHp = subject.getStat(Stat.HP);
    return subject.hp == maxHp;
});

makeFocusItem("Focus Band", false, function(subject) {
    return subject.field.random(0, 10) == 0;
});

makeItem({
    name : "Power Herb",
    informSkipChargeTurn : function(move) {
        this.subject.field.print(Text.item_messages(12, this.subject));
        if (move.additional) {
            move.additional(this.subject);
        }
        move.accuracy = move.accuracy_;
        var effect = new StatusEffect("ChargeMoveEffect");
        effect.turns = 0;
        effect.informFinishedSubjectExecution = function() {
            this.subject.removeStatus(this);
        };
        this.subject.applyStatus(this.subject, effect);
        this.consume();
        return true;
    }
});

makeItem({
    name : "Iron Ball",
    getState : null,
    // @stat SPEED, 6, Iron Ball
    statModifier : function(field, stat, subject) {
        if (subject != this.subject)
            return null;
        if (stat != Stat.SPEED)
            return null;
        return [0.5, 3];
    },
    vulnerability : function(user, target) {
        if (target != this.subject) {
            return -1;
        }
        return Type.GROUND;
    },
});

makeItem({
    name : "Griseous Orb",
    informRemoveItem : function() {
        // Griseous Orb is usable by any pokemon in Gen 5, but cannot be
        // tricked on or off Giratina (of any form)
        // TODO: Prevent Griseous Orb from being tricked onto Giratina
        var subject = this.subject;
        if (subject.species == "Giratina-o") {
            return true;
        }
        return false;
    }
});