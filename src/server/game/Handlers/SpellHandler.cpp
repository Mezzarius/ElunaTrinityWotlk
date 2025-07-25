/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "WorldSession.h"
#include "Common.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Log.h"
#include "DBCStores.h"
#include "GameClient.h"
#include "GameObject.h"
#include "GameObjectAI.h"
#include "Item.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellAuraEffects.h"
#ifdef ELUNA
#include "LuaEngine.h"
#endif
#include "SpellMgr.h"
#include "SpellPackets.h"
#include "Totem.h"
#include "TotemPackets.h"
#include "World.h"
#include "WorldPacket.h"

//npcbot
#include "bot_ai.h"
#include "botdatamgr.h"
#include "botmgr.h"
//end npcbot

void WorldSession::HandleClientCastFlags(WorldPacket& recvPacket, uint8 castFlags, SpellCastTargets& targets)
{
    // some spell cast packet including more data (for projectiles?)
    if (castFlags & 0x02)
    {
        // not sure about these two
        float elevation, speed;
        recvPacket >> elevation;
        recvPacket >> speed;

        targets.SetElevation(elevation);
        targets.SetSpeed(speed);

        uint8 hasMovementData;
        recvPacket >> hasMovementData;
        if (hasMovementData)
        {
            recvPacket.SetOpcode(recvPacket.read<uint32>());
            HandleMovementOpcodes(recvPacket);
        }
    }
}

void WorldSession::HandleUseItemOpcode(WorldPacket& recvPacket)
{
    /// @todo add targets.read() check
    Player* pUser = _player;

    uint8 bagIndex, slot, castFlags;
    uint8 castCount;                                        // next cast if exists (single or not)
    ObjectGuid itemGUID;
    uint32 glyphIndex;                                      // something to do with glyphs?
    uint32 spellId;                                         // cast spell id

    recvPacket >> bagIndex >> slot >> castCount >> spellId >> itemGUID >> glyphIndex >> castFlags;

    if (glyphIndex >= MAX_GLYPH_SLOT_INDEX)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    Item* pItem = pUser->GetUseableItemByPos(bagIndex, slot);
    if (!pItem)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    if (pItem->GetGUID() != itemGUID)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    TC_LOG_DEBUG("network", "WORLD: CMSG_USE_ITEM packet, bagIndex: {}, slot: {}, castCount: {}, spellId: {}, Item: {}, glyphIndex: {}, data length = {}", bagIndex, slot, castCount, spellId, pItem->GetEntry(), glyphIndex, (uint32)recvPacket.size());

    ItemTemplate const* proto = pItem->GetTemplate();
    if (!proto)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, nullptr);
        return;
    }

    // some item classes can be used only in equipped state
    if (proto->InventoryType != INVTYPE_NON_EQUIP && !pItem->IsEquipped())
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, nullptr);
        return;
    }

    InventoryResult msg = pUser->CanUseItem(pItem);
    if (msg != EQUIP_ERR_OK)
    {
        pUser->SendEquipError(msg, pItem, nullptr);
        return;
    }

    // only allow conjured consumable, bandage, poisons (all should have the 2^21 item flag set in DB)
    if (proto->Class == ITEM_CLASS_CONSUMABLE && !proto->HasFlag(ITEM_FLAG_IGNORE_DEFAULT_ARENA_RESTRICTIONS) && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, nullptr);
        return;
    }

    // don't allow items banned in arena
    if (proto->HasFlag(ITEM_FLAG_NOT_USEABLE_IN_ARENA) && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, nullptr);
        return;
    }

    if (pUser->IsInCombat())
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto->Spells[i].SpellId))
            {
                if (!spellInfo->CanBeUsedInCombat())
                {
                    pUser->SendEquipError(EQUIP_ERR_NOT_IN_COMBAT, pItem, nullptr);
                    return;
                }
            }
        }
    }

    // check also  BIND_WHEN_PICKED_UP and BIND_QUEST_ITEM for .additem or .additemset case by GM (not binded at adding to inventory)
    if (pItem->GetTemplate()->Bonding == BIND_WHEN_USE || pItem->GetTemplate()->Bonding == BIND_WHEN_PICKED_UP || pItem->GetTemplate()->Bonding == BIND_QUEST_ITEM)
    {
        if (!pItem->IsSoulBound())
        {
            pItem->SetState(ITEM_CHANGED, pUser);
            pItem->SetBinding(true);
        }
    }

    SpellCastTargets targets;
    targets.Read(recvPacket, pUser);
    HandleClientCastFlags(recvPacket, castFlags, targets);

    // Note: If script stop casting it must send appropriate data to client to prevent stuck item in gray state.
    if (!sScriptMgr->OnItemUse(pUser, pItem, targets))
    {
        // no script or script not process request by self
        pUser->CastItemUseSpell(pItem, targets, castCount, glyphIndex);
    }
}

void WorldSession::HandleOpenItemOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_OPEN_ITEM packet, data length = {}", (uint32)recvPacket.size());

    Player* player = GetPlayer();

    // ignore for remote control state
    if (player->IsCharmed())
        return;

    // additional check, client outputs message on its own
    if (!player->IsAlive())
    {
        player->SendEquipError(EQUIP_ERR_PLAYER_DEAD, nullptr, nullptr);
        return;
    }

    uint8 bagIndex, slot;
    recvPacket >> bagIndex >> slot;

    TC_LOG_INFO("network", "bagIndex: {}, slot: {}", bagIndex, slot);

    Item* item = player->GetItemByPos(bagIndex, slot);
    if (!item)
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, nullptr, nullptr);
        return;
    }

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
    {
        player->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, nullptr);
        return;
    }

    // Verify that the bag is an actual bag or wrapped item that can be used "normally"
    if (!proto->HasFlag(ITEM_FLAG_HAS_LOOT) && !item->IsWrapped())
    {
        player->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, item, nullptr);
        TC_LOG_ERROR("entities.player.cheat", "Possible hacking attempt: Player {} {} tried to open item [{}, entry: {}] which is not openable!",
            player->GetName(), player->GetGUID().ToString(), item->GetGUID().ToString(), proto->ItemId);
        return;
    }

    // locked item
    uint32 lockId = proto->LockID;
    if (lockId)
    {
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

        if (!lockInfo)
        {
            player->SendEquipError(EQUIP_ERR_ITEM_LOCKED, item, nullptr);
            TC_LOG_ERROR("network", "WORLD::OpenItem: item {} has an unknown lockId: {}!", item->GetGUID().ToString(), lockId);
            return;
        }

        // was not unlocked yet
        if (item->IsLocked())
        {
            player->SendEquipError(EQUIP_ERR_ITEM_LOCKED, item, nullptr);
            return;
        }
    }

    if (item->IsWrapped())
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_GIFT_BY_ITEM);
        stmt->setUInt32(0, item->GetGUID().GetCounter());
        _queryProcessor.AddCallback(CharacterDatabase.AsyncQuery(stmt)
            .WithPreparedCallback(std::bind(&WorldSession::HandleOpenWrappedItemCallback, this, item->GetPos(), item->GetGUID(), std::placeholders::_1)));
    }
    else
        player->SendLoot(item->GetGUID(), LOOT_CORPSE);
}

void WorldSession::HandleOpenWrappedItemCallback(uint16 pos, ObjectGuid itemGuid, PreparedQueryResult result)
{
    if (!GetPlayer())
        return;

    Item* item = GetPlayer()->GetItemByPos(pos);
    if (!item)
        return;

    if (item->GetGUID() != itemGuid || !item->IsWrapped()) // during getting result, gift was swapped with another item
        return;

    if (!result)
    {
        TC_LOG_ERROR("network", "Wrapped item {} does't have record in character_gifts table and will deleted", itemGuid.ToString());
        GetPlayer()->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        return;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    Field* fields = result->Fetch();
    uint32 entry = fields[0].GetUInt32();
    uint32 flags = fields[1].GetUInt32();

    item->SetGuidValue(ITEM_FIELD_GIFTCREATOR, ObjectGuid::Empty);
    item->SetEntry(entry);
    item->SetUInt32Value(ITEM_FIELD_FLAGS, flags);
    item->SetUInt32Value(ITEM_FIELD_MAXDURABILITY, item->GetTemplate()->MaxDurability);
    item->SetState(ITEM_CHANGED, GetPlayer());

    GetPlayer()->SaveInventoryAndGoldToDB(trans);

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GIFT);
    stmt->setUInt32(0, itemGuid.GetCounter());
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);
}

void WorldSession::HandleGameObjectUseOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_GAMEOBJ_USE Message [{}]", guid.ToString());

    if (GameObject* obj = GetPlayer()->GetGameObjectIfCanInteractWith(guid))
    {
        // ignore for remote control state
        if (GetPlayer()->IsCharmed())
            if (!(GetPlayer()->IsOnVehicle(GetPlayer()->GetCharmed()) || GetPlayer()->IsMounted()) && !obj->GetGOInfo()->IsUsableMounted())
                return;

        obj->Use(GetPlayer());
    }
}

void WorldSession::HandleGameobjectReportUse(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    recvPacket >> guid;

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_GAMEOBJ_REPORT_USE Message [{}]", guid.ToString());

    // ignore for remote control state
    if (_player->IsCharmed())
        return;

    if (GameObject* go = GetPlayer()->GetGameObjectIfCanInteractWith(guid))
    {
#ifdef ELUNA
        if (Eluna* e = GetPlayer()->GetEluna())
            if (e->OnGameObjectUse(_player, go))
                return;
#endif
        if (go->AI()->OnReportUse(_player))
            return;

        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT, go->GetEntry());
    }
}

void WorldSession::HandleCastSpellOpcode(WorldPacket& recvPacket)
{
    uint32 spellId;
    uint8  castCount, castFlags;
    recvPacket >> castCount >> spellId >> castFlags;
    TriggerCastFlags triggerFlag = TRIGGERED_NONE;

    TC_LOG_DEBUG("network", "WORLD: got cast spell packet, castCount: {}, spellId: {}, castFlags: {}, data length = {}", castCount, spellId, castFlags, (uint32)recvPacket.size());

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown spell id {}", spellId);
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    if (spellInfo->IsPassive())
    {
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    // client provided targets
    SpellCastTargets targets;
    targets.Read(recvPacket, _player);
    HandleClientCastFlags(recvPacket, castFlags, targets);

    // not have spell in spellbook
    if (!_player->HasActiveSpell(spellId))
    {
        bool allow = false;

        // allow casting of unknown spells for special lock cases
        if (GameObject* go = targets.GetGOTarget())
            if (go->GetSpellForLock(_player) == spellInfo)
                allow = true;

        // allow casting of spells triggered by clientside periodic trigger auras
        if (_player->HasAuraTypeWithTriggerSpell(SPELL_AURA_PERIODIC_TRIGGER_SPELL_FROM_CLIENT, spellId))
        {
            allow = true;
            triggerFlag = TRIGGERED_FULL_MASK;
        }

        if (!allow)
            return;
    }

    // Client is resending autoshot cast opcode when other spell is cast during shoot rotation
    // Skip it to prevent "interrupt" message
    // Also check targets! target may have changed and we need to interrupt current spell
    if (spellInfo->IsAutoRepeatRangedSpell())
    {
        if (Spell* spell = _player->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        {
            if (spell->m_spellInfo == spellInfo && spell->m_targets.GetUnitTargetGUID() == targets.GetUnitTargetGUID())
            {
                recvPacket.rfinish();
                return;
            }
        }
    }

    // auto-selection buff level base at target level (in spellInfo)
    // TODO: is this even necessary? client already seems to send correct rank for "standard" buffs
    if (spellInfo->IsPositive())
        if (Unit* target = targets.GetUnitTarget())
        {
            SpellInfo const* actualSpellInfo = spellInfo->GetAuraRankForLevel(target->GetLevel());

            // if rank not found then function return NULL but in explicit cast case original spell can be cast and later failed with appropriate error message
            if (actualSpellInfo)
                spellInfo = actualSpellInfo;
        }

    Spell* spell = new Spell(_player, spellInfo, triggerFlag);
    spell->m_fromClient = true;
    spell->m_cast_count = castCount;                       // set count of casts
    spell->prepare(targets);
}

void WorldSession::HandleCancelCastOpcode(WorldPackets::Spells::CancelCast& cancelCast)
{
    if (_player->IsCharmed())
        return;

    if (_player->IsNonMeleeSpellCast(false))
        _player->InterruptNonMeleeSpells(false, cancelCast.SpellID, false);
}

void WorldSession::HandleCancelAuraOpcode(WorldPackets::Spells::CancelAura& cancelAura)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(cancelAura.SpellID);
    if (!spellInfo)
        return;

    // not allow remove spells with attr SPELL_ATTR0_CANT_CANCEL
    if (spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL))
        return;

    // channeled spell case (it currently cast then)
    if (spellInfo->IsChanneled())
    {
        if (Spell* curSpell = _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (curSpell->m_spellInfo->Id == cancelAura.SpellID)
                _player->InterruptSpell(CURRENT_CHANNELED_SPELL);
        return;
    }

    // non channeled case:
    // don't allow remove non positive spells
    // don't allow cancelling passive auras (some of them are visible)
    if (!spellInfo->IsPositive() || spellInfo->IsPassive())
        return;

    // maybe should only remove one buff when there are multiple?
    _player->RemoveOwnedAura(cancelAura.SpellID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);

    // If spell being removed is a resource tracker, see if player was tracking both (herbs / minerals) and remove the other
    if (sWorld->getBoolConfig(CONFIG_ALLOW_TRACK_BOTH_RESOURCES) && spellInfo->HasAura(SPELL_AURA_TRACK_RESOURCES))
    {
        Unit::AuraEffectList const& auraEffects = _player->GetAuraEffectsByType(SPELL_AURA_TRACK_RESOURCES);
        if (!auraEffects.empty())
        {
            // Build list of spell IDs to cancel. Trying to cancel the aura while iterating
            //  over AuraEffectList caused "incompatible iterator" errors on second pass
            std::list<uint32> spellIDs;

            for (Unit::AuraEffectList::const_iterator auraEffect = auraEffects.begin(); auraEffect != auraEffects.end(); ++auraEffect)
                spellIDs.push_back((*auraEffect)->GetId());

            // Remove all auras related to resource tracking (only Herbs and Minerals in 3.3.5a)
            for (std::list<uint32>::iterator it = spellIDs.begin(); it != spellIDs.end(); ++it)
                _player->RemoveOwnedAura(*it, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
        }
    }
}

void WorldSession::HandlePetCancelAuraOpcode(WorldPackets::Spells::PetCancelAura& packet)
{
    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(packet.SpellID);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown PET spell id {}", packet.SpellID);
        return;
    }

    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, packet.PetGUID);

    if (!pet)
    {
        TC_LOG_ERROR("network", "HandlePetCancelAura: Attempt to cancel an aura for non-existant {} by player '{}'", packet.PetGUID.ToString(), GetPlayer()->GetName());
        return;
    }

    if (pet != GetPlayer()->GetGuardianPet() && pet != GetPlayer()->GetCharmed())
    {
        TC_LOG_ERROR("network", "HandlePetCancelAura: {} is not a pet of player '{}'", packet.PetGUID.ToString(), GetPlayer()->GetName());
        return;
    }

    if (!pet->IsAlive())
    {
        pet->SendPetActionFeedback(PetActionFeedback::Dead, 0);
        return;
    }

    pet->RemoveOwnedAura(packet.SpellID, ObjectGuid::Empty, 0, AURA_REMOVE_BY_CANCEL);
}

void WorldSession::HandleCancelGrowthAuraOpcode(WorldPackets::Spells::CancelGrowthAura& /*cancelGrowthAura*/)
{
    _player->RemoveAurasByType(SPELL_AURA_MOD_SCALE, [](AuraApplication const* aurApp)
    {
        SpellInfo const* spellInfo = aurApp->GetBase()->GetSpellInfo();
        return !spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL) && spellInfo->IsPositive() && !spellInfo->IsPassive();
    });
}

void WorldSession::HandleCancelMountAuraOpcode(WorldPackets::Spells::CancelMountAura& /*cancelMountAura*/)
{
    _player->RemoveAurasByType(SPELL_AURA_MOUNTED, [](AuraApplication const* aurApp)
    {
        SpellInfo const* spellInfo = aurApp->GetBase()->GetSpellInfo();
        return !spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL) && spellInfo->IsPositive() && !spellInfo->IsPassive();
    });
}

void WorldSession::HandleCancelAutoRepeatSpellOpcode(WorldPackets::Spells::CancelAutoRepeatSpell& /*cancelAutoRepeatSpell*/)
{
    // may be better send SMSG_CANCEL_AUTO_REPEAT?
    // cancel and prepare for deleting
    _player->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
}

void WorldSession::HandleCancelChanneling(WorldPackets::Spells::CancelChannelling& cancelChanneling)
{
    Unit* mover = GetGameClient()->GetActivelyMovedUnit();

    // ignore for remote control state (for player case)
    if (!mover)
        return;

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(cancelChanneling.ChannelSpell);
    if (!spellInfo)
        return;

    // not allow remove spells with attr SPELL_ATTR0_CANT_CANCEL
    if (spellInfo->HasAttribute(SPELL_ATTR0_CANT_CANCEL))
        return;

    Spell* spell = mover->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
    if (!spell || spell->GetSpellInfo()->Id != spellInfo->Id)
        return;

    mover->InterruptSpell(CURRENT_CHANNELED_SPELL);
}

void WorldSession::HandleTotemDestroyed(WorldPackets::Totem::TotemDestroyed& totemDestroyed)
{
    // ignore for remote control state
    if (_player->IsCharmed())
        return;

    uint8 slotId = totemDestroyed.Slot;
    slotId += SUMMON_SLOT_TOTEM_FIRE;

    if (slotId >= MAX_TOTEM_SLOT)
        return;

    if (!_player->m_SummonSlot[slotId])
        return;

    Creature* totem = ObjectAccessor::GetCreature(*_player, _player->m_SummonSlot[slotId]);
    if (totem && totem->IsTotem())
        totem->ToTotem()->UnSummon();
}

void WorldSession::HandleSelfResOpcode(WorldPacket & /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_SELF_RES");                  // empty opcode

    if (SpellInfo const* spell = sSpellMgr->GetSpellInfo(_player->GetUInt32Value(PLAYER_SELF_RES_SPELL)))
    {
        if (_player->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION) && !spell->HasAttribute(SPELL_ATTR7_BYPASS_NO_RESURRECT_AURA))
            return; // silent return, client should display error by itself and not send this opcode

        _player->CastSpell(_player, spell->Id);
        _player->SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);
    }
}

void WorldSession::HandleSpellClick(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    // this will get something not in world. crash
    Creature* unit = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!unit)
        return;

    /// @todo Unit::SetCharmedBy: 28782 is not in world but 0 is trying to charm it! -> crash
    if (!unit->IsInWorld())
        return;

    unit->HandleSpellClick(_player);
}

void WorldSession::HandleMirrorImageDataRequest(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_GET_MIRRORIMAGE_DATA");
    ObjectGuid guid;
    recvData >> guid;

    // Get unit for which data is needed by client
    Unit* unit = ObjectAccessor::GetUnit(*_player, guid);
    if (!unit)
        return;

    //npcbot
    if (unit->GetTypeId() == TYPEID_UNIT)
    {
        CreatureOutfitContainer const& outfits = sObjectMgr->GetCreatureOutfitMap();
        CreatureOutfitContainer::const_iterator it = outfits.find(unit->GetEntry());
        if (it != outfits.end())
        {
            WorldPacket data(SMSG_MIRRORIMAGE_DATA, 68);
            data << uint64(guid);
            data << uint32(unit->GetNativeDisplayId()); // displayId
            data << uint8(it->second.race);             // race
            data << uint8(it->second.gender);           // gender
            data << uint8(unit->GetClass());            // class
            data << uint8(it->second.skin);             // skin
            data << uint8(it->second.face);             // face
            data << uint8(it->second.hair);             // hair
            data << uint8(it->second.haircolor);        // haircolor
            data << uint8(it->second.facialhair);       // facialhair
            data << uint32(0);                          // guildId

            // item displays
            for (uint8 i = 0; i != MAX_CREATURE_OUTFIT_DISPLAYS; ++i)
                data << uint32(it->second.outfit[i]);

            SendPacket(&data);
            return;
        }

        //npcbot minion without a record in outfits table
        //OR
        //npcbot's mirror image
        Creature const* bot = unit->ToCreature();
        if (!bot->IsNPCBot() && unit->HasAuraType(SPELL_AURA_CLONE_CASTER))
            if (Unit const* creator = unit->GetAuraEffectsByType(SPELL_AURA_CLONE_CASTER).front()->GetCaster())
                if (creator->IsNPCBot())
                    bot = creator->ToCreature();

        if (bot->IsNPCBot())
        {
            NpcBotAppearanceData const* appearData = BotDataMgr::SelectNpcBotAppearance(bot->GetEntry());

            WorldPacket data(SMSG_MIRRORIMAGE_DATA, 68);
            data << uint64(guid);
            data << uint32(bot->GetDisplayId());                                       // displayId
            data << uint8(bot->GetRace());                                             // race
            data << uint8(appearData ? appearData->gender : (uint8)bot->GetGender());  // gender
            data << uint8(bot->GetBotAI()->GetPlayerClass());                          // class
            data << uint8(appearData ? appearData->skin : 0);                          // skin
            data << uint8(appearData ? appearData->face : 0);                          // face
            data << uint8(appearData ? appearData->hair : 0);                          // hair
            data << uint8(appearData ? appearData->haircolor : 0);                     // haircolor
            data << uint8(appearData ? appearData->features : 0);                      // facialhair
            data << uint32(0);                                                         // guildId

            static uint8 const botItemSlots[MAX_CREATURE_OUTFIT_DISPLAYS] =
            {
                BOT_SLOT_HEAD,
                BOT_SLOT_SHOULDERS,
                BOT_SLOT_BODY,
                BOT_SLOT_CHEST,
                BOT_SLOT_WAIST,
                BOT_SLOT_LEGS,
                BOT_SLOT_FEET,
                BOT_SLOT_WRIST,
                BOT_SLOT_HANDS,
                BOT_SLOT_BACK,
                0//tabard
            };

            // Display items in visible slots
            for (uint8 i = 0; i != MAX_CREATURE_OUTFIT_DISPLAYS; ++i)
            {
                uint8 slot = botItemSlots[i];
                //Items not displayed on bot: tabard, head, back
                if (slot == 0 ||
                    (slot == BOT_SLOT_HEAD && BotMgr::ShowEquippedHelm() == false) ||
                    (slot == BOT_SLOT_BACK && BotMgr::ShowEquippedCloak() == false))
                {
                    data << uint32(0);
                    continue;
                }

                uint32 display_id = bot->GetBotAI()->GetEquipDisplayId(slot);
                if (display_id)
                    data << uint32(display_id);
                else
                {
                    //don't allow to go naked
                    if (slot == BOT_SLOT_CHEST)
                        data << uint32(CHEST_HALISCAN);
                    else if (slot == BOT_SLOT_LEGS)
                        data << uint32(LEGS_HALISCAN);
                    else
                        data << uint32(0);
                }
            }

            SendPacket(&data);
            return;
        }
    }
    //end npcbot

    if (!unit->HasAuraType(SPELL_AURA_CLONE_CASTER))
        return;

    // Get creator of the unit (SPELL_AURA_CLONE_CASTER does not stack)
    Unit* creator = unit->GetAuraEffectsByType(SPELL_AURA_CLONE_CASTER).front()->GetCaster();
    if (!creator)
        return;

    WorldPacket data(SMSG_MIRRORIMAGE_DATA, 68);
    data << uint64(guid);
    data << uint32(creator->GetDisplayId());
    data << uint8(creator->GetRace());
    data << uint8(creator->GetGender());
    data << uint8(creator->GetClass());

    if (Player* player = creator->ToPlayer())
    {
        data << uint8(player->GetSkinId());
        data << uint8(player->GetFaceId());
        data << uint8(player->GetHairStyleId());
        data << uint8(player->GetHairColorId());
        data << uint8(player->GetFacialStyle());
        data << uint32(player->GetGuildId());

        static EquipmentSlots const itemSlots[] =
        {
            EQUIPMENT_SLOT_HEAD,
            EQUIPMENT_SLOT_SHOULDERS,
            EQUIPMENT_SLOT_BODY,
            EQUIPMENT_SLOT_CHEST,
            EQUIPMENT_SLOT_WAIST,
            EQUIPMENT_SLOT_LEGS,
            EQUIPMENT_SLOT_FEET,
            EQUIPMENT_SLOT_WRISTS,
            EQUIPMENT_SLOT_HANDS,
            EQUIPMENT_SLOT_BACK,
            EQUIPMENT_SLOT_TABARD,
            EQUIPMENT_SLOT_END
        };

        // Display items in visible slots
        for (EquipmentSlots const* itr = &itemSlots[0]; *itr != EQUIPMENT_SLOT_END; ++itr)
        {
            if (*itr == EQUIPMENT_SLOT_HEAD && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
                data << uint32(0);
            else if (*itr == EQUIPMENT_SLOT_BACK && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
                data << uint32(0);
            else if (Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, *itr))
                data << uint32(item->GetTemplate()->DisplayInfoID);
            else
                data << uint32(0);
        }
    }
    else
    {
        // Skip player data for creatures
        data << uint8(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
    }

    SendPacket(&data);
}

void WorldSession::HandleUpdateProjectilePosition(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_UPDATE_PROJECTILE_POSITION");

    ObjectGuid casterGuid;
    uint32 spellId;
    uint8 castCount;
    float x, y, z;    // Position of missile hit

    recvPacket >> casterGuid;
    recvPacket >> spellId;
    recvPacket >> castCount;
    recvPacket >> x;
    recvPacket >> y;
    recvPacket >> z;

    Unit* caster = ObjectAccessor::GetUnit(*_player, casterGuid);
    if (!caster)
        return;

    Spell* spell = caster->FindCurrentSpellBySpellId(spellId);
    if (!spell || !spell->m_targets.HasDst())
        return;

    Position pos = *spell->m_targets.GetDstPos();
    pos.Relocate(x, y, z);
    spell->m_targets.ModDst(pos);

    // we changed dest, recalculate flight time
    spell->RecalculateDelayMomentForDst();

    WorldPacket data(SMSG_SET_PROJECTILE_POSITION, 21);
    data << uint64(casterGuid);
    data << uint8(castCount);
    data << float(x);
    data << float(y);
    data << float(z);
    caster->SendMessageToSet(&data, true);
}
