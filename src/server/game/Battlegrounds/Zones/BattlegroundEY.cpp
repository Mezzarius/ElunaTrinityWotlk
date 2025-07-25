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

#include "BattlegroundEY.h"
#include "BattlegroundMgr.h"
#include "BattlegroundPackets.h"
#include "Creature.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Random.h"
#include "Util.h"
#include "WorldStatePackets.h"

//npcbot
#include "botdatamgr.h"
//end npcbot

// these variables aren't used outside of this file, so declare them only here
uint32 BG_EY_HonorScoreTicks[BG_HONOR_MODE_NUM] =
{
    260, // normal honor
    160  // holiday
};

void BattlegroundEYScore::BuildObjectivesBlock(WorldPackets::Battleground::PVPLogData_Player& playerData)
{
    playerData.Stats = { FlagCaptures };
}

BattlegroundEY::BattlegroundEY()
{
    m_BuffChange = true;
    BgObjects.resize(BG_EY_OBJECT_MAX);
    BgCreatures.resize(BG_EY_CREATURES_MAX);
    m_Points_Trigger[FEL_REAVER] = TR_FEL_REAVER_BUFF;
    m_Points_Trigger[BLOOD_ELF] = TR_BLOOD_ELF_BUFF;
    m_Points_Trigger[DRAENEI_RUINS] = TR_DRAENEI_RUINS_BUFF;
    m_Points_Trigger[MAGE_TOWER] = TR_MAGE_TOWER_BUFF;
    m_HonorScoreTics[TEAM_ALLIANCE] = 0;
    m_HonorScoreTics[TEAM_HORDE] = 0;
    m_TeamPointsCount[TEAM_ALLIANCE] = 0;
    m_TeamPointsCount[TEAM_HORDE] = 0;
    m_FlagKeeper.Clear();
    m_DroppedFlagGUID.Clear();
    m_FlagCapturedBgObjectType = 0;
    m_FlagState = BG_EY_FLAG_STATE_ON_BASE;
    m_FlagsTimer = 0;
    m_TowerCapCheckTimer = 0;
    m_PointAddingTimer = 0;
    m_HonorTics = 0;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        m_PointOwnedByTeam[i] = EY_POINT_NO_OWNER;
        m_PointState[i] = EY_POINT_STATE_UNCONTROLLED;
        m_PointBarStatus[i] = BG_EY_PROGRESS_BAR_STATE_MIDDLE;
    }

    for (uint8 i = 0; i < 2 * EY_POINTS_MAX; ++i)
        m_CurrentPointPlayersCount[i] = 0;
}

BattlegroundEY::~BattlegroundEY() { }

void BattlegroundEY::PostUpdateImpl(uint32 diff)
{
    if (GetStatus() == STATUS_IN_PROGRESS)
    {
        m_PointAddingTimer -= diff;
        if (m_PointAddingTimer <= 0)
        {
            m_PointAddingTimer = BG_EY_FPOINTS_TICK_TIME;
            if (m_TeamPointsCount[TEAM_ALLIANCE] > 0)
                AddPoints(ALLIANCE, BG_EY_TickPoints[m_TeamPointsCount[TEAM_ALLIANCE] - 1]);
            if (m_TeamPointsCount[TEAM_HORDE] > 0)
                AddPoints(HORDE, BG_EY_TickPoints[m_TeamPointsCount[TEAM_HORDE] - 1]);
        }

        if (m_FlagState == BG_EY_FLAG_STATE_WAIT_RESPAWN || m_FlagState == BG_EY_FLAG_STATE_ON_GROUND)
        {
            m_FlagsTimer -= diff;

            if (m_FlagsTimer < 0)
            {
                m_FlagsTimer = 0;
                if (m_FlagState == BG_EY_FLAG_STATE_WAIT_RESPAWN)
                    RespawnFlag(true);
                else
                    RespawnFlagAfterDrop();
            }
        }

        m_TowerCapCheckTimer -= diff;
        if (m_TowerCapCheckTimer <= 0)
        {
            //check if player joined point
            /*I used this order of calls, because although we will check if one player is in gameobject's distance 2 times
              but we can count of players on current point in CheckSomeoneLeftPoint
            */
            CheckSomeoneJoinedPoint();
            //check if player left point
            CheckSomeoneLeftPoint();
            UpdatePointStatuses();
            m_TowerCapCheckTimer = BG_EY_FPOINTS_TICK_TIME;
        }
    }
}

void BattlegroundEY::StartingEventCloseDoors()
{
    SpawnBGObject(BG_EY_OBJECT_DOOR_A, RESPAWN_IMMEDIATELY);
    SpawnBGObject(BG_EY_OBJECT_DOOR_H, RESPAWN_IMMEDIATELY);

    for (uint32 i = BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER; i < BG_EY_OBJECT_MAX; ++i)
        SpawnBGObject(i, RESPAWN_ONE_DAY);
}

void BattlegroundEY::StartingEventOpenDoors()
{
    SpawnBGObject(BG_EY_OBJECT_DOOR_A, RESPAWN_ONE_DAY);
    SpawnBGObject(BG_EY_OBJECT_DOOR_H, RESPAWN_ONE_DAY);

    for (uint32 i = BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER; i <= BG_EY_OBJECT_FLAG_NETHERSTORM; ++i)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    for (uint32 i = 0; i < EY_POINTS_MAX; ++i)
    {
        //randomly spawn buff
        uint8 buff = urand(0, 2);
        SpawnBGObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + buff + i * 3, RESPAWN_IMMEDIATELY);
    }

    // Achievement: Flurry
    StartTimedAchievement(ACHIEVEMENT_TIMED_TYPE_EVENT, BG_EY_EVENT_START_BATTLE);
}

void BattlegroundEY::AddPoints(uint32 Team, uint32 Points)
{
    TeamId team_index = GetTeamIndexByTeamId(Team);
    m_TeamScores[team_index] += Points;
    m_HonorScoreTics[team_index] += Points;
    if (m_HonorScoreTics[team_index] >= m_HonorTics)
    {
        RewardHonorToTeam(GetBonusHonorFromKill(1), Team);
        m_HonorScoreTics[team_index] -= m_HonorTics;
    }
    UpdateTeamScore(team_index);
}

void BattlegroundEY::CheckSomeoneJoinedPoint()
{
    GameObject* obj = nullptr;
    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        obj = GetBgMap()->GetGameObject(BgObjects[BG_EY_OBJECT_TOWER_CAP_FEL_REAVER + i]);

        if (obj)
        {
            uint8 j = 0;
            while (j < m_PlayersNearPoint[EY_POINTS_MAX].size())
            {
                if (m_PlayersNearPoint[EY_POINTS_MAX][j].IsCreature())
                {
                    Creature const* bot = BotDataMgr::FindBot(m_PlayersNearPoint[EY_POINTS_MAX][j].GetEntry());
                    if (!bot)
                    {
                        TC_LOG_ERROR("bg.battleground", "BattlegroundEY:CheckSomeoneJoinedPoint: Bot {} could not be found!", m_PlayersNearPoint[EY_POINTS_MAX][j].GetEntry());
                        ++j;
                        continue;
                    }
                    if (bot->IsAlive() && !bot->HasInvisibilityAura() && !bot->HasStealthAura() && bot->IsWithinDistInMap(obj, BG_EY_POINT_RADIUS))
                    {
                        m_PlayersNearPoint[i].push_back(m_PlayersNearPoint[EY_POINTS_MAX][j]);
                        m_PlayersNearPoint[EY_POINTS_MAX].erase(m_PlayersNearPoint[EY_POINTS_MAX].begin() + j);
                    }
                    else
                        ++j;

                    continue;
                }
                Player* player = ObjectAccessor::FindPlayer(m_PlayersNearPoint[EY_POINTS_MAX][j]);
                if (!player)
                {
                    TC_LOG_ERROR("bg.battleground", "BattlegroundEY:CheckSomeoneJoinedPoint: Player ({}) could not be found!", m_PlayersNearPoint[EY_POINTS_MAX][j].ToString());
                    ++j;
                    continue;
                }
                if (player->CanCaptureTowerPoint() && player->IsWithinDistInMap(obj, BG_EY_POINT_RADIUS))
                {
                    //player joined point!
                    //show progress bar
                    player->SendUpdateWorldState(PROGRESS_BAR_PERCENT_GREY, BG_EY_PROGRESS_BAR_PERCENT_GREY);
                    player->SendUpdateWorldState(PROGRESS_BAR_STATUS, m_PointBarStatus[i]);
                    player->SendUpdateWorldState(PROGRESS_BAR_SHOW, BG_EY_PROGRESS_BAR_SHOW);
                    //add player to point
                    m_PlayersNearPoint[i].push_back(m_PlayersNearPoint[EY_POINTS_MAX][j]);
                    //remove player from "free space"
                    m_PlayersNearPoint[EY_POINTS_MAX].erase(m_PlayersNearPoint[EY_POINTS_MAX].begin() + j);
                }
                else
                    ++j;
            }
        }
    }
}

void BattlegroundEY::CheckSomeoneLeftPoint()
{
    //reset current point counts
    for (uint8 i = 0; i < 2*EY_POINTS_MAX; ++i)
        m_CurrentPointPlayersCount[i] = 0;
    GameObject* obj = nullptr;
    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        obj = GetBgMap()->GetGameObject(BgObjects[BG_EY_OBJECT_TOWER_CAP_FEL_REAVER + i]);

        if (obj)
        {
            uint8 j = 0;
            while (j < m_PlayersNearPoint[i].size())
            {
                if (m_PlayersNearPoint[i][j].IsCreature())
                {
                    Creature const* bot = BotDataMgr::FindBot(m_PlayersNearPoint[i][j].GetEntry());
                    if (!bot)
                    {
                        TC_LOG_ERROR("bg.battleground", "BattlegroundEY:CheckSomeoneLeftPoint: Bot {} could not be found!", m_PlayersNearPoint[i][j].GetEntry());
                        m_PlayersNearPoint[EY_POINTS_MAX].push_back(m_PlayersNearPoint[i][j]);
                        m_PlayersNearPoint[i].erase(m_PlayersNearPoint[i].begin() + j);
                        continue;
                    }
                    if (!(bot->IsAlive() && !bot->HasInvisibilityAura() && !bot->HasStealthAura() && bot->IsWithinDistInMap(obj, BG_EY_POINT_RADIUS)))
                    {
                        m_PlayersNearPoint[EY_POINTS_MAX].push_back(m_PlayersNearPoint[i][j]);
                        m_PlayersNearPoint[i].erase(m_PlayersNearPoint[i].begin() + j);
                    }
                    else
                    {
                        m_CurrentPointPlayersCount[2 * i + GetBotTeamId(bot->GetGUID())]++;
                        ++j;
                    }

                    continue;
                }
                Player* player = ObjectAccessor::FindPlayer(m_PlayersNearPoint[i][j]);
                if (!player)
                {
                    TC_LOG_ERROR("bg.battleground", "BattlegroundEY:CheckSomeoneLeftPoint Player ({}) could not be found!", m_PlayersNearPoint[i][j].ToString());
                    //move non-existing players to "free space" - this will cause many errors showing in log, but it is a very important bug
                    m_PlayersNearPoint[EY_POINTS_MAX].push_back(m_PlayersNearPoint[i][j]);
                    m_PlayersNearPoint[i].erase(m_PlayersNearPoint[i].begin() + j);
                    continue;
                }
                if (!player->CanCaptureTowerPoint() || !player->IsWithinDistInMap(obj, BG_EY_POINT_RADIUS))
                    //move player out of point (add him to players that are out of points
                {
                    m_PlayersNearPoint[EY_POINTS_MAX].push_back(m_PlayersNearPoint[i][j]);
                    m_PlayersNearPoint[i].erase(m_PlayersNearPoint[i].begin() + j);
                    player->SendUpdateWorldState(PROGRESS_BAR_SHOW, BG_EY_PROGRESS_BAR_DONT_SHOW);
                }
                else
                {
                    //player is neat flag, so update count:
                    m_CurrentPointPlayersCount[2 * i + GetTeamIndexByTeamId(player->GetTeam())]++;
                    ++j;
                }
            }
        }
    }
}

void BattlegroundEY::UpdatePointStatuses()
{
    for (uint8 point = 0; point < EY_POINTS_MAX; ++point)
    {
        if (m_PlayersNearPoint[point].empty())
            continue;
        //count new point bar status:
        m_PointBarStatus[point] += (m_CurrentPointPlayersCount[2 * point] - m_CurrentPointPlayersCount[2 * point + 1] < BG_EY_POINT_MAX_CAPTURERS_COUNT) ? m_CurrentPointPlayersCount[2 * point] - m_CurrentPointPlayersCount[2 * point + 1] : BG_EY_POINT_MAX_CAPTURERS_COUNT;

        if (m_PointBarStatus[point] > BG_EY_PROGRESS_BAR_ALI_CONTROLLED)
            //point is fully alliance's
            m_PointBarStatus[point] = BG_EY_PROGRESS_BAR_ALI_CONTROLLED;
        if (m_PointBarStatus[point] < BG_EY_PROGRESS_BAR_HORDE_CONTROLLED)
            //point is fully horde's
            m_PointBarStatus[point] = BG_EY_PROGRESS_BAR_HORDE_CONTROLLED;

        uint32 pointOwnerTeamId = 0;
        //find which team should own this point
        if (m_PointBarStatus[point] <= BG_EY_PROGRESS_BAR_NEUTRAL_LOW)
            pointOwnerTeamId = HORDE;
        else if (m_PointBarStatus[point] >= BG_EY_PROGRESS_BAR_NEUTRAL_HIGH)
            pointOwnerTeamId = ALLIANCE;
        else
            pointOwnerTeamId = EY_POINT_NO_OWNER;

        for (uint8 i = 0; i < m_PlayersNearPoint[point].size(); ++i)
        {
            if (m_PlayersNearPoint[point][i].IsCreature())
            {
                if (Creature const* bot = BotDataMgr::FindBot(m_PlayersNearPoint[point][i].GetEntry()))
                {
                    uint32 botteam = GetBotTeam(bot->GetGUID());

                    if (pointOwnerTeamId != m_PointOwnedByTeam[point])
                    {
                        //point was uncontrolled and player is from team which captured point
                        if (m_PointState[point] == EY_POINT_STATE_UNCONTROLLED && botteam == pointOwnerTeamId)
                            EventBotTeamCapturedPoint(bot, point);

                        //point was under control and player isn't from team which controlled it
                        if (m_PointState[point] == EY_POINT_UNDER_CONTROL && botteam != m_PointOwnedByTeam[point])
                            EventBotTeamLostPoint(bot, point);
                    }

                    /// @workaround The original AreaTrigger is covered by a bigger one and not triggered on client side.
                    if (point == FEL_REAVER && m_PointOwnedByTeam[point] == botteam)
                        if (m_FlagState && GetFlagPickerGUID() == bot->GetGUID())
                            if (bot->GetDistance(2044.0f, 1729.729f, 1190.03f) < 3.0f)
                                EventBotCapturedFlag(const_cast<Creature*>(bot), BG_EY_OBJECT_FLAG_FEL_REAVER);
                }

                continue;
            }

            Player* player = ObjectAccessor::FindPlayer(m_PlayersNearPoint[point][i]);
            if (player)
            {
                player->SendUpdateWorldState(PROGRESS_BAR_STATUS, m_PointBarStatus[point]);
                //if point owner changed we must evoke event!
                if (pointOwnerTeamId != m_PointOwnedByTeam[point])
                {
                    //point was uncontrolled and player is from team which captured point
                    if (m_PointState[point] == EY_POINT_STATE_UNCONTROLLED && player->GetTeam() == pointOwnerTeamId)
                        EventTeamCapturedPoint(player, point);

                    //point was under control and player isn't from team which controlled it
                    if (m_PointState[point] == EY_POINT_UNDER_CONTROL && player->GetTeam() != m_PointOwnedByTeam[point])
                        EventTeamLostPoint(player, point);
                }

                /// @workaround The original AreaTrigger is covered by a bigger one and not triggered on client side.
                if (point == FEL_REAVER && m_PointOwnedByTeam[point] == player->GetTeam())
                    if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                        if (player->GetDistance(2044.0f, 1729.729f, 1190.03f) < 3.0f)
                            EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_FEL_REAVER);
            }
        }
    }
}

void BattlegroundEY::UpdateTeamScore(uint32 Team)
{
    uint32 score = GetTeamScore(Team);

    if (score >= BG_EY_MAX_TEAM_SCORE)
    {
        score = BG_EY_MAX_TEAM_SCORE;
        if (Team == TEAM_ALLIANCE)
            EndBattleground(ALLIANCE);
        else
            EndBattleground(HORDE);
    }

    if (Team == TEAM_ALLIANCE)
        UpdateWorldState(EY_ALLIANCE_RESOURCES, score);
    else
        UpdateWorldState(EY_HORDE_RESOURCES, score);
}

void BattlegroundEY::EndBattleground(uint32 winner)
{
    // Win reward
    if (winner == ALLIANCE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);
    if (winner == HORDE)
        RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);
    // Complete map reward
    RewardHonorToTeam(GetBonusHonorFromKill(1), ALLIANCE);
    RewardHonorToTeam(GetBonusHonorFromKill(1), HORDE);

    Battleground::EndBattleground(winner);
}

void BattlegroundEY::UpdatePointsCount(uint32 Team)
{
    if (Team == ALLIANCE)
        UpdateWorldState(EY_ALLIANCE_BASE, m_TeamPointsCount[TEAM_ALLIANCE]);
    else
        UpdateWorldState(EY_HORDE_BASE, m_TeamPointsCount[TEAM_HORDE]);
}

void BattlegroundEY::UpdatePointsIcons(uint32 Team, uint32 Point)
{
    //we MUST firstly send 0, after that we can send 1!!!
    if (m_PointState[Point] == EY_POINT_UNDER_CONTROL)
    {
        UpdateWorldState(m_PointsIconStruct[Point].WorldStateControlIndex, 0);
        if (Team == ALLIANCE)
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateAllianceControlledIndex, 1);
        else
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateHordeControlledIndex, 1);
    }
    else
    {
        if (Team == ALLIANCE)
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateAllianceControlledIndex, 0);
        else
            UpdateWorldState(m_PointsIconStruct[Point].WorldStateHordeControlledIndex, 0);
        UpdateWorldState(m_PointsIconStruct[Point].WorldStateControlIndex, 1);
    }
}

void BattlegroundEY::AddPlayer(Player* player)
{
    bool const isInBattleground = IsPlayerInBattleground(player->GetGUID());
    Battleground::AddPlayer(player);
    if (!isInBattleground)
        PlayerScores[player->GetGUID().GetCounter()] = new BattlegroundEYScore(player->GetGUID());

    m_PlayersNearPoint[EY_POINTS_MAX].push_back(player->GetGUID());
}

//npcbot
void BattlegroundEY::AddBot(Creature* bot)
{
    bool const isInBattleground = IsPlayerInBattleground(bot->GetGUID());
    Battleground::AddBot(bot);
    if (!isInBattleground)
        BotScores[bot->GetEntry()] = new BattlegroundEYScore(bot->GetGUID());

    m_PlayersNearPoint[EY_POINTS_MAX].push_back(bot->GetGUID());
}
//end npcbot

void BattlegroundEY::RemovePlayer(Player* player, ObjectGuid guid, uint32 /*team*/)
{
    // sometimes flag aura not removed :(
    for (int j = EY_POINTS_MAX; j >= 0; --j)
    {
        for (size_t i = 0; i < m_PlayersNearPoint[j].size(); ++i)
            if (m_PlayersNearPoint[j][i] == guid)
                m_PlayersNearPoint[j].erase(m_PlayersNearPoint[j].begin() + i);
    }
    if (IsFlagPickedup())
    {
        if (m_FlagKeeper == guid)
        {
            if (player)
                EventPlayerDroppedFlag(player);
            else
            {
                SetFlagPicker(ObjectGuid::Empty);
                RespawnFlag(true);
            }
        }
    }
}

//npcbot
void BattlegroundEY::RemoveBot(ObjectGuid guid)
{
    // sometimes flag aura not removed :(
    for (int j = EY_POINTS_MAX; j >= 0; --j)
    {
        for (size_t i = 0; i < m_PlayersNearPoint[j].size(); ++i)
            if (m_PlayersNearPoint[j][i] == guid)
                m_PlayersNearPoint[j].erase(m_PlayersNearPoint[j].begin() + i);
    }
    if (IsFlagPickedup())
    {
        if (m_FlagKeeper == guid)
        {
            if (Creature const* bot = BotDataMgr::FindBot(guid.GetEntry()))
                EventBotDroppedFlag(const_cast<Creature*>(bot));
            else
            {
                SetFlagPicker(ObjectGuid::Empty);
                RespawnFlag(true);
            }
        }
    }
}
//end npcbot

void BattlegroundEY::HandleAreaTrigger(Player* player, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    if (!player->IsAlive())                                  //hack code, must be removed later
        return;

    switch (trigger)
    {
        case TR_BLOOD_ELF_POINT:
            if (m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[BLOOD_ELF] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_BLOOD_ELF);
            break;
        case TR_FEL_REAVER_POINT:
            if (m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[FEL_REAVER] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_FEL_REAVER);
            break;
        case TR_MAGE_TOWER_POINT:
            if (m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[MAGE_TOWER] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_MAGE_TOWER);
            break;
        case TR_DRAENEI_RUINS_POINT:
            if (m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[DRAENEI_RUINS] == player->GetTeam())
                if (m_FlagState && GetFlagPickerGUID() == player->GetGUID())
                    EventPlayerCapturedFlag(player, BG_EY_OBJECT_FLAG_DRAENEI_RUINS);
            break;
        case 4512:
        case 4515:
        case 4517:
        case 4519:
        case 4530:
        case 4531:
        case 4568:
        case 4569:
        case 4570:
        case 4571:
        case 5866:
            break;
        default:
            Battleground::HandleAreaTrigger(player, trigger);
            break;
    }
}

//npcbot
void BattlegroundEY::HandleBotAreaTrigger(Creature* bot, uint32 trigger)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    if (!bot->IsAlive())
        return;

    ObjectGuid botguid = bot->GetGUID();
    uint32 botteam = GetBotTeam(botguid);

    switch (trigger)
    {
        case TR_BLOOD_ELF_POINT:
            if (m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[BLOOD_ELF] == botteam)
                if (m_FlagState && GetFlagPickerGUID() == botguid)
                    EventBotCapturedFlag(bot, BG_EY_OBJECT_FLAG_BLOOD_ELF);
            break;
        case TR_FEL_REAVER_POINT:
            if (m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[FEL_REAVER] == botteam)
                if (m_FlagState && GetFlagPickerGUID() == botguid)
                    EventBotCapturedFlag(bot, BG_EY_OBJECT_FLAG_FEL_REAVER);
            break;
        case TR_MAGE_TOWER_POINT:
            if (m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[MAGE_TOWER] == botteam)
                if (m_FlagState && GetFlagPickerGUID() == botguid)
                    EventBotCapturedFlag(bot, BG_EY_OBJECT_FLAG_MAGE_TOWER);
            break;
        case TR_DRAENEI_RUINS_POINT:
            if (m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL && m_PointOwnedByTeam[DRAENEI_RUINS] == botteam)
                if (m_FlagState && GetFlagPickerGUID() == botguid)
                    EventBotCapturedFlag(bot, BG_EY_OBJECT_FLAG_DRAENEI_RUINS);
            break;
        case 4512:
        case 4515:
        case 4517:
        case 4519:
        case 4530:
        case 4531:
        case 4568:
        case 4569:
        case 4570:
        case 4571:
        case 5866:
            break;
        default:
            Battleground::HandleBotAreaTrigger(bot, trigger);
            break;
    }
}
//end npcbot

bool BattlegroundEY::SetupBattleground()
{
        // doors
    if (!AddObject(BG_EY_OBJECT_DOOR_A, BG_OBJECT_A_DOOR_EY_ENTRY, 2527.6f, 1596.91f, 1262.13f, -3.12414f, -0.173642f, -0.001515f, 0.98477f, -0.008594f, RESPAWN_IMMEDIATELY)
        || !AddObject(BG_EY_OBJECT_DOOR_H, BG_OBJECT_H_DOOR_EY_ENTRY, 1803.21f, 1539.49f, 1261.09f, 3.14159f, 0.173648f, 0, 0.984808f, 0, RESPAWN_IMMEDIATELY)
        // banners (alliance)
        || !AddObject(BG_EY_OBJECT_A_BANNER_FEL_REAVER_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2057.46f, 1735.07f, 1187.91f, -0.925024f, 0, 0, 0.446198f, -0.894934f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_FEL_REAVER_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2032.25f, 1729.53f, 1190.33f, 1.8675f, 0, 0, 0.803857f, 0.594823f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_FEL_REAVER_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2092.35f, 1775.46f, 1187.08f, -0.401426f, 0, 0, 0.199368f, -0.979925f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_BLOOD_ELF_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2047.19f, 1349.19f, 1189.0f, -1.62316f, 0, 0, 0.725374f, -0.688354f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_BLOOD_ELF_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2074.32f, 1385.78f, 1194.72f, 0.488692f, 0, 0, 0.241922f, 0.970296f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_BLOOD_ELF_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2025.13f, 1386.12f, 1192.74f, 2.3911f, 0, 0, 0.930418f, 0.366501f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2276.8f, 1400.41f, 1196.33f, 2.44346f, 0, 0, 0.939693f, 0.34202f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2305.78f, 1404.56f, 1199.38f, 1.74533f, 0, 0, 0.766044f, 0.642788f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_DRAENEI_RUINS_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2245.4f, 1366.41f, 1195.28f, 2.21657f, 0, 0, 0.894934f, 0.446198f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_MAGE_TOWER_CENTER, BG_OBJECT_A_BANNER_EY_ENTRY, 2270.84f, 1784.08f, 1186.76f, 2.42601f, 0, 0, 0.936672f, 0.350207f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_MAGE_TOWER_LEFT, BG_OBJECT_A_BANNER_EY_ENTRY, 2269.13f, 1737.7f, 1186.66f, 0.994838f, 0, 0, 0.477159f, 0.878817f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_A_BANNER_MAGE_TOWER_RIGHT, BG_OBJECT_A_BANNER_EY_ENTRY, 2300.86f, 1741.25f, 1187.7f, -0.785398f, 0, 0, 0.382683f, -0.92388f, RESPAWN_ONE_DAY)
        // banners (horde)
        || !AddObject(BG_EY_OBJECT_H_BANNER_FEL_REAVER_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2057.46f, 1735.07f, 1187.91f, -0.925024f, 0, 0, 0.446198f, -0.894934f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_FEL_REAVER_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2032.25f, 1729.53f, 1190.33f, 1.8675f, 0, 0, 0.803857f, 0.594823f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_FEL_REAVER_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2092.35f, 1775.46f, 1187.08f, -0.401426f, 0, 0, 0.199368f, -0.979925f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_BLOOD_ELF_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2047.19f, 1349.19f, 1189.0f, -1.62316f, 0, 0, 0.725374f, -0.688354f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_BLOOD_ELF_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2074.32f, 1385.78f, 1194.72f, 0.488692f, 0, 0, 0.241922f, 0.970296f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_BLOOD_ELF_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2025.13f, 1386.12f, 1192.74f, 2.3911f, 0, 0, 0.930418f, 0.366501f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2276.8f, 1400.41f, 1196.33f, 2.44346f, 0, 0, 0.939693f, 0.34202f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2305.78f, 1404.56f, 1199.38f, 1.74533f, 0, 0, 0.766044f, 0.642788f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_DRAENEI_RUINS_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2245.4f, 1366.41f, 1195.28f, 2.21657f, 0, 0, 0.894934f, 0.446198f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_MAGE_TOWER_CENTER, BG_OBJECT_H_BANNER_EY_ENTRY, 2270.84f, 1784.08f, 1186.76f, 2.42601f, 0, 0, 0.936672f, 0.350207f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_MAGE_TOWER_LEFT, BG_OBJECT_H_BANNER_EY_ENTRY, 2269.13f, 1737.7f, 1186.66f, 0.994838f, 0, 0, 0.477159f, 0.878817f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_H_BANNER_MAGE_TOWER_RIGHT, BG_OBJECT_H_BANNER_EY_ENTRY, 2300.86f, 1741.25f, 1187.7f, -0.785398f, 0, 0, 0.382683f, -0.92388f, RESPAWN_ONE_DAY)
        // banners (natural)
        || !AddObject(BG_EY_OBJECT_N_BANNER_FEL_REAVER_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2057.46f, 1735.07f, 1187.91f, -0.925024f, 0, 0, 0.446198f, -0.894934f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_FEL_REAVER_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2032.25f, 1729.53f, 1190.33f, 1.8675f, 0, 0, 0.803857f, 0.594823f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_FEL_REAVER_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2092.35f, 1775.46f, 1187.08f, -0.401426f, 0, 0, 0.199368f, -0.979925f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2047.19f, 1349.19f, 1189.0f, -1.62316f, 0, 0, 0.725374f, -0.688354f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2074.32f, 1385.78f, 1194.72f, 0.488692f, 0, 0, 0.241922f, 0.970296f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_BLOOD_ELF_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2025.13f, 1386.12f, 1192.74f, 2.3911f, 0, 0, 0.930418f, 0.366501f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2276.8f, 1400.41f, 1196.33f, 2.44346f, 0, 0, 0.939693f, 0.34202f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2305.78f, 1404.56f, 1199.38f, 1.74533f, 0, 0, 0.766044f, 0.642788f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_DRAENEI_RUINS_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2245.4f, 1366.41f, 1195.28f, 2.21657f, 0, 0, 0.894934f, 0.446198f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_CENTER, BG_OBJECT_N_BANNER_EY_ENTRY, 2270.84f, 1784.08f, 1186.76f, 2.42601f, 0, 0, 0.936672f, 0.350207f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_LEFT, BG_OBJECT_N_BANNER_EY_ENTRY, 2269.13f, 1737.7f, 1186.66f, 0.994838f, 0, 0, 0.477159f, 0.878817f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_N_BANNER_MAGE_TOWER_RIGHT, BG_OBJECT_N_BANNER_EY_ENTRY, 2300.86f, 1741.25f, 1187.7f, -0.785398f, 0, 0, 0.382683f, -0.92388f, RESPAWN_ONE_DAY)
        // flags
        || !AddObject(BG_EY_OBJECT_FLAG_NETHERSTORM, BG_OBJECT_FLAG2_EY_ENTRY, 2174.782227f, 1569.054688f, 1160.361938f, -1.448624f, 0, 0, 0.662620f, -0.748956f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_FEL_REAVER, BG_OBJECT_FLAG1_EY_ENTRY, 2044.28f, 1729.68f, 1189.96f, -0.017453f, 0, 0, 0.008727f, -0.999962f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_BLOOD_ELF, BG_OBJECT_FLAG1_EY_ENTRY, 2048.83f, 1393.65f, 1194.49f, 0.20944f, 0, 0, 0.104528f, 0.994522f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_DRAENEI_RUINS, BG_OBJECT_FLAG1_EY_ENTRY, 2286.56f, 1402.36f, 1197.11f, 3.72381f, 0, 0, 0.957926f, -0.287016f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_FLAG_MAGE_TOWER, BG_OBJECT_FLAG1_EY_ENTRY, 2284.48f, 1731.23f, 1189.99f, 2.89725f, 0, 0, 0.992546f, 0.121869f, RESPAWN_ONE_DAY)
        // tower cap
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_FEL_REAVER, BG_OBJECT_FR_TOWER_CAP_EY_ENTRY, 2024.600708f, 1742.819580f, 1195.157715f, 2.443461f, 0, 0, 0.939693f, 0.342020f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_BLOOD_ELF, BG_OBJECT_BE_TOWER_CAP_EY_ENTRY, 2050.493164f, 1372.235962f, 1194.563477f, 1.710423f, 0, 0, 0.754710f, 0.656059f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_DRAENEI_RUINS, BG_OBJECT_DR_TOWER_CAP_EY_ENTRY, 2301.010498f, 1386.931641f, 1197.183472f, 1.570796f, 0, 0, 0.707107f, 0.707107f, RESPAWN_ONE_DAY)
        || !AddObject(BG_EY_OBJECT_TOWER_CAP_MAGE_TOWER, BG_OBJECT_HU_TOWER_CAP_EY_ENTRY, 2282.121582f, 1760.006958f, 1189.707153f, 1.919862f, 0, 0, 0.819152f, 0.573576f, RESPAWN_ONE_DAY)
)
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundEY: Failed to spawn some objects. The battleground was not created.");
        return false;
    }

    //buffs
    for (int i = 0; i < EY_POINTS_MAX; ++i)
    {
        AreaTriggerEntry const* at = sAreaTriggerStore.LookupEntry(m_Points_Trigger[i]);
        if (!at)
        {
            TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Unknown trigger: {}", m_Points_Trigger[i]);
            continue;
        }
        if (!AddObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + i * 3, Buff_Entries[0], at->Pos.X, at->Pos.Y, at->Pos.Z, 0.907571f, 0, 0, 0.438371f, 0.898794f, RESPAWN_ONE_DAY)
            || !AddObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + i * 3 + 1, Buff_Entries[1], at->Pos.X, at->Pos.Y, at->Pos.Z, 0.907571f, 0, 0, 0.438371f, 0.898794f, RESPAWN_ONE_DAY)
            || !AddObject(BG_EY_OBJECT_SPEEDBUFF_FEL_REAVER + i * 3 + 2, Buff_Entries[2], at->Pos.X, at->Pos.Y, at->Pos.Z, 0.907571f, 0, 0, 0.438371f, 0.898794f, RESPAWN_ONE_DAY)
)
            TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Could not spawn Speedbuff Fel Reaver.");
    }

    WorldSafeLocsEntry const* sg = sWorldSafeLocsStore.LookupEntry(EY_GRAVEYARD_MAIN_ALLIANCE);
    if (!sg || !AddSpiritGuide(EY_SPIRIT_MAIN_ALLIANCE, sg->Loc.X, sg->Loc.Y, sg->Loc.Z, 3.124139f, TEAM_ALLIANCE))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundEY: Failed to spawn spirit guide. The battleground was not created.");
        return false;
    }

    sg = sWorldSafeLocsStore.LookupEntry(EY_GRAVEYARD_MAIN_HORDE);
    if (!sg || !AddSpiritGuide(EY_SPIRIT_MAIN_HORDE, sg->Loc.X, sg->Loc.Y, sg->Loc.Z, 3.193953f, TEAM_HORDE))
    {
        TC_LOG_ERROR("sql.sql", "BatteGroundEY: Failed to spawn spirit guide. The battleground was not created.");
        return false;
    }

    return true;
}

void BattlegroundEY::Reset()
{
    //call parent's class reset
    Battleground::Reset();

    m_TeamScores[TEAM_ALLIANCE] = 0;
    m_TeamScores[TEAM_HORDE] = 0;
    m_TeamPointsCount[TEAM_ALLIANCE] = 0;
    m_TeamPointsCount[TEAM_HORDE] = 0;
    m_HonorScoreTics[TEAM_ALLIANCE] = 0;
    m_HonorScoreTics[TEAM_HORDE] = 0;
    m_FlagState = BG_EY_FLAG_STATE_ON_BASE;
    m_FlagCapturedBgObjectType = 0;
    m_FlagKeeper.Clear();
    m_DroppedFlagGUID.Clear();
    m_PointAddingTimer = 0;
    m_TowerCapCheckTimer = 0;
    bool isBGWeekend = sBattlegroundMgr->IsBGWeekend(GetTypeID());
    m_HonorTics = (isBGWeekend) ? BG_EY_EYWeekendHonorTicks : BG_EY_NotEYWeekendHonorTicks;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        m_PointOwnedByTeam[i] = EY_POINT_NO_OWNER;
        m_PointState[i] = EY_POINT_STATE_UNCONTROLLED;
        m_PointBarStatus[i] = BG_EY_PROGRESS_BAR_STATE_MIDDLE;
        m_PlayersNearPoint[i].clear();
        m_PlayersNearPoint[i].reserve(15);                  //tip size
    }
    m_PlayersNearPoint[EY_PLAYERS_OUT_OF_POINTS].clear();
    m_PlayersNearPoint[EY_PLAYERS_OUT_OF_POINTS].reserve(30);
}

void BattlegroundEY::RespawnFlag(bool send_message)
{
    if (m_FlagCapturedBgObjectType > 0)
        SpawnBGObject(m_FlagCapturedBgObjectType, RESPAWN_ONE_DAY);

    m_FlagCapturedBgObjectType = 0;
    m_FlagState = BG_EY_FLAG_STATE_ON_BASE;
    SpawnBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM, RESPAWN_IMMEDIATELY);

    if (send_message)
    {
        SendBroadcastText(BG_EY_TEXT_FLAG_RESET, CHAT_MSG_BG_SYSTEM_NEUTRAL);
        PlaySoundToAll(BG_EY_SOUND_FLAG_RESET);             // flags respawned sound...
    }

    UpdateWorldState(NETHERSTORM_FLAG, 1);
}

void BattlegroundEY::RespawnFlagAfterDrop()
{
    RespawnFlag(true);

    GameObject* obj = GetBgMap()->GetGameObject(GetDroppedFlagGUID());

    if (obj)
        obj->Delete();
    else
        TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Unknown dropped flag ({}).", GetDroppedFlagGUID().ToString());

    SetDroppedFlagGUID(ObjectGuid::Empty);
}

void BattlegroundEY::HandleKillPlayer(Player* player, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Battleground::HandleKillPlayer(player, killer);
    EventPlayerDroppedFlag(player);
}

//npcbot
void BattlegroundEY::HandleBotKillPlayer(Creature* killer, Player* victim)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Battleground::HandleBotKillPlayer(killer, victim);
    EventPlayerDroppedFlag(victim);
}

void BattlegroundEY::HandleBotKillBot(Creature* killer, Creature* victim)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Battleground::HandleBotKillBot(killer, victim);
    EventBotDroppedFlag(victim);
}

void BattlegroundEY::HandlePlayerKillBot(Creature* victim, Player* killer)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    Battleground::HandlePlayerKillBot(victim, killer);
    EventBotDroppedFlag(victim);
}
//end npcbot

void BattlegroundEY::EventPlayerDroppedFlag(Player* player)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        // if not running, do not cast things at the dropper player, neither send unnecessary messages
        // just take off the aura
        if (IsFlagPickedup() && GetFlagPickerGUID() == player->GetGUID())
        {
            SetFlagPicker(ObjectGuid::Empty);
            player->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);
        }
        return;
    }

    if (!IsFlagPickedup())
        return;

    if (GetFlagPickerGUID() != player->GetGUID())
        return;

    SetFlagPicker(ObjectGuid::Empty);
    player->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);
    m_FlagState = BG_EY_FLAG_STATE_ON_GROUND;
    m_FlagsTimer = BG_EY_FLAG_RESPAWN_TIME;
    player->CastSpell(player, SPELL_RECENTLY_DROPPED_FLAG, true);
    player->CastSpell(player, BG_EY_PLAYER_DROPPED_FLAG_SPELL, true);
    //this does not work correctly :((it should remove flag carrier name)
    UpdateWorldState(NETHERSTORM_FLAG_STATE_HORDE, BG_EY_FLAG_STATE_WAIT_RESPAWN);
    UpdateWorldState(NETHERSTORM_FLAG_STATE_ALLIANCE, BG_EY_FLAG_STATE_WAIT_RESPAWN);

    if (player->GetTeam() == ALLIANCE)
        SendBroadcastText(BG_EY_TEXT_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_ALLIANCE);
    else
        SendBroadcastText(BG_EY_TEXT_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_HORDE);
}

//end npcbot
void BattlegroundEY::EventBotDroppedFlag(Creature* bot)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
    {
        if (IsFlagPickedup() && GetFlagPickerGUID() == bot->GetGUID())
        {
            SetFlagPicker(ObjectGuid::Empty);
            bot->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);
        }
        return;
    }

    if (!IsFlagPickedup())
        return;

    if (GetFlagPickerGUID() != bot->GetGUID())
        return;

    SetFlagPicker(ObjectGuid::Empty);
    bot->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);
    m_FlagState = BG_EY_FLAG_STATE_ON_GROUND;
    m_FlagsTimer = BG_EY_FLAG_RESPAWN_TIME;
    bot->CastSpell(bot, SPELL_RECENTLY_DROPPED_FLAG, true);
    bot->CastSpell(bot, BG_EY_PLAYER_DROPPED_FLAG_SPELL, true);
    //this does not work correctly :((it should remove flag carrier name)
    UpdateWorldState(NETHERSTORM_FLAG_STATE_HORDE, BG_EY_FLAG_STATE_WAIT_RESPAWN);
    UpdateWorldState(NETHERSTORM_FLAG_STATE_ALLIANCE, BG_EY_FLAG_STATE_WAIT_RESPAWN);

    if (GetBotTeamId(bot->GetGUID()) == TEAM_ALLIANCE)
        SendBroadcastText(BG_EY_TEXT_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_ALLIANCE);
    else
        SendBroadcastText(BG_EY_TEXT_FLAG_DROPPED, CHAT_MSG_BG_SYSTEM_HORDE);
}
//end npcbot

void BattlegroundEY::EventPlayerClickedOnFlag(Player* player, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS || IsFlagPickedup() || !player->IsWithinDistInMap(target_obj, 10))
        return;

    if (player->GetTeam() == ALLIANCE)
    {
        UpdateWorldState(NETHERSTORM_FLAG_STATE_ALLIANCE, BG_EY_FLAG_STATE_ON_PLAYER);
        PlaySoundToAll(BG_EY_SOUND_FLAG_PICKED_UP_ALLIANCE);
    }
    else
    {
        UpdateWorldState(NETHERSTORM_FLAG_STATE_HORDE, BG_EY_FLAG_STATE_ON_PLAYER);
        PlaySoundToAll(BG_EY_SOUND_FLAG_PICKED_UP_HORDE);
    }

    if (m_FlagState == BG_EY_FLAG_STATE_ON_BASE)
        UpdateWorldState(NETHERSTORM_FLAG, 0);
    m_FlagState = BG_EY_FLAG_STATE_ON_PLAYER;

    SpawnBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM, RESPAWN_ONE_DAY);
    SetFlagPicker(player->GetGUID());
    //get flag aura on player
    player->CastSpell(player, BG_EY_NETHERSTORM_FLAG_SPELL, true);
    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    if (player->GetTeam() == ALLIANCE)
        SendBroadcastText(BG_EY_TEXT_TAKEN_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(BG_EY_TEXT_TAKEN_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, player);
}

//end npcbot
void BattlegroundEY::EventBotClickedOnFlag(Creature* bot, GameObject* target_obj)
{
    if (GetStatus() != STATUS_IN_PROGRESS || IsFlagPickedup() || !bot->IsWithinDistInMap(target_obj, 10))
        return;

    TeamId botteamid = GetBotTeamId(bot->GetGUID());

    if (botteamid == TEAM_ALLIANCE)
    {
        UpdateWorldState(NETHERSTORM_FLAG_STATE_ALLIANCE, BG_EY_FLAG_STATE_ON_PLAYER);
        PlaySoundToAll(BG_EY_SOUND_FLAG_PICKED_UP_ALLIANCE);
    }
    else
    {
        UpdateWorldState(NETHERSTORM_FLAG_STATE_HORDE, BG_EY_FLAG_STATE_ON_PLAYER);
        PlaySoundToAll(BG_EY_SOUND_FLAG_PICKED_UP_HORDE);
    }

    if (m_FlagState == BG_EY_FLAG_STATE_ON_BASE)
        UpdateWorldState(NETHERSTORM_FLAG, 0);
    m_FlagState = BG_EY_FLAG_STATE_ON_PLAYER;

    SpawnBGObject(BG_EY_OBJECT_FLAG_NETHERSTORM, RESPAWN_ONE_DAY);
    SetFlagPicker(bot->GetGUID());
    //get flag aura on player
    bot->CastSpell(bot, BG_EY_NETHERSTORM_FLAG_SPELL, true);
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    if (botteamid == TEAM_ALLIANCE)
        SendBroadcastText(BG_EY_TEXT_TAKEN_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
    else
        SendBroadcastText(BG_EY_TEXT_TAKEN_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, bot);
}
//end npcbot

void BattlegroundEY::EventTeamLostPoint(Player* player, uint32 Point)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    //Natural point
    uint32 Team = m_PointOwnedByTeam[Point];

    if (!Team)
        return;

    if (Team == ALLIANCE)
    {
        m_TeamPointsCount[TEAM_ALLIANCE]--;
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance + 1, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance + 2, RESPAWN_ONE_DAY);
    }
    else
    {
        m_TeamPointsCount[TEAM_HORDE]--;
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde + 1, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde + 2, RESPAWN_ONE_DAY);
    }

    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType, RESPAWN_IMMEDIATELY);
    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType + 1, RESPAWN_IMMEDIATELY);
    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType + 2, RESPAWN_IMMEDIATELY);

    //buff isn't despawned

    m_PointOwnedByTeam[Point] = EY_POINT_NO_OWNER;
    m_PointState[Point] = EY_POINT_NO_OWNER;

    if (Team == ALLIANCE)
        SendBroadcastText(m_LosingPointTypes[Point].MessageIdAlliance, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(m_LosingPointTypes[Point].MessageIdHorde, CHAT_MSG_BG_SYSTEM_HORDE, player);

    UpdatePointsIcons(Team, Point);
    UpdatePointsCount(Team);

    //remove bonus honor aura trigger creature when node is lost
    DelCreature(Point + 6);//NULL checks are in DelCreature! 0-5 spirit guides

    //npcbot
    RelocateDeadPlayers(BgCreatures[Point]);
    //end npcbot
}

//npcbot
void BattlegroundEY::EventBotTeamLostPoint(Creature const* bot, uint32 Point)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    //Natural point
    uint32 Team = m_PointOwnedByTeam[Point];

    if (!Team)
        return;

    if (Team == ALLIANCE)
    {
        m_TeamPointsCount[TEAM_ALLIANCE]--;
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance + 1, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeAlliance + 2, RESPAWN_ONE_DAY);
    }
    else
    {
        m_TeamPointsCount[TEAM_HORDE]--;
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde + 1, RESPAWN_ONE_DAY);
        SpawnBGObject(m_LosingPointTypes[Point].DespawnObjectTypeHorde + 2, RESPAWN_ONE_DAY);
    }

    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType, RESPAWN_IMMEDIATELY);
    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType + 1, RESPAWN_IMMEDIATELY);
    SpawnBGObject(m_LosingPointTypes[Point].SpawnNeutralObjectType + 2, RESPAWN_IMMEDIATELY);

    //buff isn't despawned

    m_PointOwnedByTeam[Point] = EY_POINT_NO_OWNER;
    m_PointState[Point] = EY_POINT_NO_OWNER;

    if (Team == ALLIANCE)
        SendBroadcastText(m_LosingPointTypes[Point].MessageIdAlliance, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
    else
        SendBroadcastText(m_LosingPointTypes[Point].MessageIdHorde, CHAT_MSG_BG_SYSTEM_HORDE, bot);

    UpdatePointsIcons(Team, Point);
    UpdatePointsCount(Team);

    //remove bonus honor aura trigger creature when node is lost
    DelCreature(Point + 6);//NULL checks are in DelCreature! 0-5 spirit guides

    RelocateDeadPlayers(BgCreatures[Point]);
}
//end npcbot

void BattlegroundEY::EventTeamCapturedPoint(Player* player, uint32 Point)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint32 Team = player->GetTeam();

    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType, RESPAWN_ONE_DAY);
    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType + 1, RESPAWN_ONE_DAY);
    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType + 2, RESPAWN_ONE_DAY);

    if (Team == ALLIANCE)
    {
        m_TeamPointsCount[TEAM_ALLIANCE]++;
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance + 1, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance + 2, RESPAWN_IMMEDIATELY);
    }
    else
    {
        m_TeamPointsCount[TEAM_HORDE]++;
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde + 1, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde + 2, RESPAWN_IMMEDIATELY);
    }

    //buff isn't respawned

    m_PointOwnedByTeam[Point] = Team;
    m_PointState[Point] = EY_POINT_UNDER_CONTROL;

    if (Team == ALLIANCE)
        SendBroadcastText(m_CapturingPointTypes[Point].MessageIdAlliance, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
    else
        SendBroadcastText(m_CapturingPointTypes[Point].MessageIdHorde, CHAT_MSG_BG_SYSTEM_HORDE, player);

    if (BgCreatures[Point])
        DelCreature(Point);

    WorldSafeLocsEntry const* sg = sWorldSafeLocsStore.LookupEntry(m_CapturingPointTypes[Point].GraveyardId);
    if (!sg || !AddSpiritGuide(Point, sg->Loc.X, sg->Loc.Y, sg->Loc.Z, 3.124139f, GetTeamIndexByTeamId(Team)))
        TC_LOG_ERROR("bg.battleground", "BatteGroundEY: Failed to spawn spirit guide. point: {}, team: {}, graveyard_id: {}",
            Point, Team, m_CapturingPointTypes[Point].GraveyardId);

//    SpawnBGCreature(Point, RESPAWN_IMMEDIATELY);

    UpdatePointsIcons(Team, Point);
    UpdatePointsCount(Team);

    Creature* trigger = GetBGCreature(Point + 6, false);//0-5 spirit guides
    if (!trigger)
        trigger = AddCreature(WORLD_TRIGGER, Point+6, BG_EY_TriggerPositions[Point], GetTeamIndexByTeamId(Team));

    //add bonus honor aura trigger creature when node is accupied
    //cast bonus aura (+50% honor in 25yards)
    //aura should only apply to players who have accupied the node, set correct faction for trigger
    if (trigger)
    {
        trigger->SetFaction(Team == ALLIANCE ? FACTION_ALLIANCE_GENERIC : FACTION_HORDE_GENERIC);
        trigger->CastSpell(trigger, SPELL_HONORABLE_DEFENDER_25Y, false);
    }
}

//npcbot
void BattlegroundEY::EventBotTeamCapturedPoint(Creature const* bot, uint32 Point)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint32 Team = GetBotTeam(bot->GetGUID());

    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType, RESPAWN_ONE_DAY);
    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType + 1, RESPAWN_ONE_DAY);
    SpawnBGObject(m_CapturingPointTypes[Point].DespawnNeutralObjectType + 2, RESPAWN_ONE_DAY);

    if (Team == ALLIANCE)
    {
        m_TeamPointsCount[TEAM_ALLIANCE]++;
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance + 1, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeAlliance + 2, RESPAWN_IMMEDIATELY);
    }
    else
    {
        m_TeamPointsCount[TEAM_HORDE]++;
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde + 1, RESPAWN_IMMEDIATELY);
        SpawnBGObject(m_CapturingPointTypes[Point].SpawnObjectTypeHorde + 2, RESPAWN_IMMEDIATELY);
    }

    //buff isn't respawned

    m_PointOwnedByTeam[Point] = Team;
    m_PointState[Point] = EY_POINT_UNDER_CONTROL;

    if (Team == ALLIANCE)
        SendBroadcastText(m_CapturingPointTypes[Point].MessageIdAlliance, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
    else
        SendBroadcastText(m_CapturingPointTypes[Point].MessageIdHorde, CHAT_MSG_BG_SYSTEM_HORDE, bot);

    if (BgCreatures[Point])
        DelCreature(Point);

    WorldSafeLocsEntry const* sg = sWorldSafeLocsStore.LookupEntry(m_CapturingPointTypes[Point].GraveyardId);
    if (!sg || !AddSpiritGuide(Point, sg->Loc.X, sg->Loc.Y, sg->Loc.Z, 3.124139f, GetTeamIndexByTeamId(Team)))
        TC_LOG_ERROR("bg.battleground", "BatteGroundEY: Failed to spawn spirit guide. point: {}, team: {}, graveyard_id: {}",
            Point, Team, m_CapturingPointTypes[Point].GraveyardId);

//    SpawnBGCreature(Point, RESPAWN_IMMEDIATELY);

    UpdatePointsIcons(Team, Point);
    UpdatePointsCount(Team);

    Creature* trigger = GetBGCreature(Point + 6, false);//0-5 spirit guides
    if (!trigger)
        trigger = AddCreature(WORLD_TRIGGER, Point+6, BG_EY_TriggerPositions[Point], GetTeamIndexByTeamId(Team));

    //add bonus honor aura trigger creature when node is accupied
    //cast bonus aura (+50% honor in 25yards)
    //aura should only apply to players who have accupied the node, set correct faction for trigger
    if (trigger)
    {
        trigger->SetFaction(Team == ALLIANCE ? FACTION_ALLIANCE_GENERIC : FACTION_HORDE_GENERIC);
        trigger->CastSpell(trigger, SPELL_HONORABLE_DEFENDER_25Y, false);
    }
}
//end npcbot

void BattlegroundEY::EventPlayerCapturedFlag(Player* player, uint32 BgObjectType)
{
    if (GetStatus() != STATUS_IN_PROGRESS || GetFlagPickerGUID() != player->GetGUID())
        return;

    SetFlagPicker(ObjectGuid::Empty);
    m_FlagState = BG_EY_FLAG_STATE_WAIT_RESPAWN;
    player->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);

    player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    if (player->GetTeam() == ALLIANCE)
    {
        SendBroadcastText(BG_EY_TEXT_ALLIANCE_CAPTURED_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, player);
        PlaySoundToAll(BG_EY_SOUND_FLAG_CAPTURED_ALLIANCE);
    }
    else
    {
        SendBroadcastText(BG_EY_TEXT_HORDE_CAPTURED_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, player);
        PlaySoundToAll(BG_EY_SOUND_FLAG_CAPTURED_HORDE);
    }

    SpawnBGObject(BgObjectType, RESPAWN_IMMEDIATELY);

    m_FlagsTimer = BG_EY_FLAG_RESPAWN_TIME;
    m_FlagCapturedBgObjectType = BgObjectType;

    uint8 team_id = player->GetTeam() == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE;
    if (m_TeamPointsCount[team_id] > 0)
        AddPoints(player->GetTeam(), BG_EY_FlagPoints[m_TeamPointsCount[team_id] - 1]);

    UpdatePlayerScore(player, SCORE_FLAG_CAPTURES, 1);
}

//npcbot
void BattlegroundEY::EventBotCapturedFlag(Creature* bot, uint32 bgObjectType)
{
    if (GetStatus() != STATUS_IN_PROGRESS || GetFlagPickerGUID() != bot->GetGUID())
        return;

    TeamId botteamid = GetBotTeamId(bot->GetGUID());

    SetFlagPicker(ObjectGuid::Empty);
    m_FlagState = BG_EY_FLAG_STATE_WAIT_RESPAWN;
    bot->RemoveAurasDueToSpell(BG_EY_NETHERSTORM_FLAG_SPELL);

    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_ENTER_PVP_COMBAT);

    if (botteamid == TEAM_ALLIANCE)
    {
        SendBroadcastText(BG_EY_TEXT_ALLIANCE_CAPTURED_FLAG, CHAT_MSG_BG_SYSTEM_ALLIANCE, bot);
        PlaySoundToAll(BG_EY_SOUND_FLAG_CAPTURED_ALLIANCE);
    }
    else
    {
        SendBroadcastText(BG_EY_TEXT_HORDE_CAPTURED_FLAG, CHAT_MSG_BG_SYSTEM_HORDE, bot);
        PlaySoundToAll(BG_EY_SOUND_FLAG_CAPTURED_HORDE);
    }

    SpawnBGObject(bgObjectType, RESPAWN_IMMEDIATELY);

    m_FlagsTimer = BG_EY_FLAG_RESPAWN_TIME;
    m_FlagCapturedBgObjectType = bgObjectType;

    if (m_TeamPointsCount[botteamid] > 0)
        AddPoints(GetBotTeam(bot->GetGUID()), BG_EY_FlagPoints[m_TeamPointsCount[botteamid] - 1]);

    UpdateBotScore(bot, SCORE_FLAG_CAPTURES, 1);
}
//end npcbot

bool BattlegroundEY::UpdatePlayerScore(Player* player, uint32 type, uint32 value, bool doAddHonor)
{
    if (!Battleground::UpdatePlayerScore(player, type, value, doAddHonor))
        return false;

    switch (type)
    {
        case SCORE_FLAG_CAPTURES:
            player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_BG_OBJECTIVE_CAPTURE, EY_OBJECTIVE_CAPTURE_FLAG);
            break;
        default:
            break;
    }
    return true;
}

//end npcbot
bool BattlegroundEY::UpdateBotScore(Creature const* bot, uint32 type, uint32 value, bool doAddHonor)
{
    if (!Battleground::UpdateBotScore(bot, type, value, doAddHonor))
        return false;
    return true;
}
//end npcbot

void BattlegroundEY::FillInitialWorldStates(WorldPackets::WorldState::InitWorldStates& packet)
{
    packet.Worldstates.emplace_back(EY_HORDE_BASE, m_TeamPointsCount[TEAM_HORDE]);
    packet.Worldstates.emplace_back(EY_ALLIANCE_BASE, m_TeamPointsCount[TEAM_ALLIANCE]);
    packet.Worldstates.emplace_back(2742, 0); // Mage Tower - Horde conflict
    packet.Worldstates.emplace_back(2741, 0); // Mage Tower - Alliance conflict
    packet.Worldstates.emplace_back(2740, 0); // Fel Reaver - Horde conflict
    packet.Worldstates.emplace_back(2739, 0); // Fel Reaver - Alliance conflict
    packet.Worldstates.emplace_back(2738, 0); // Draenei - Alliance conflict
    packet.Worldstates.emplace_back(2737, 0); // Draenei - Horde conflict
    packet.Worldstates.emplace_back(2736, 0); // unk (0 at start)
    packet.Worldstates.emplace_back(2735, 0); // unk (0 at start)

    packet.Worldstates.emplace_back(DRAENEI_RUINS_HORDE_CONTROL, (m_PointOwnedByTeam[DRAENEI_RUINS] == HORDE && m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(DRAENEI_RUINS_ALLIANCE_CONTROL, (m_PointOwnedByTeam[DRAENEI_RUINS] == ALLIANCE && m_PointState[DRAENEI_RUINS] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(DRAENEI_RUINS_UNCONTROL, (m_PointState[DRAENEI_RUINS] != EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(MAGE_TOWER_ALLIANCE_CONTROL, (m_PointOwnedByTeam[MAGE_TOWER] == ALLIANCE && m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(MAGE_TOWER_HORDE_CONTROL, (m_PointOwnedByTeam[MAGE_TOWER] == HORDE && m_PointState[MAGE_TOWER] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(MAGE_TOWER_UNCONTROL, (m_PointState[MAGE_TOWER] != EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(FEL_REAVER_HORDE_CONTROL, (m_PointOwnedByTeam[FEL_REAVER] == HORDE && m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(FEL_REAVER_ALLIANCE_CONTROL, (m_PointOwnedByTeam[FEL_REAVER] == ALLIANCE && m_PointState[FEL_REAVER] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(FEL_REAVER_UNCONTROL, (m_PointState[FEL_REAVER] != EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(BLOOD_ELF_HORDE_CONTROL, (m_PointOwnedByTeam[BLOOD_ELF] == HORDE && m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(BLOOD_ELF_ALLIANCE_CONTROL, (m_PointOwnedByTeam[BLOOD_ELF] == ALLIANCE && m_PointState[BLOOD_ELF] == EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(BLOOD_ELF_UNCONTROL, (m_PointState[BLOOD_ELF] != EY_POINT_UNDER_CONTROL) ? 1 : 0);
    packet.Worldstates.emplace_back(NETHERSTORM_FLAG, (m_FlagState == BG_EY_FLAG_STATE_ON_BASE) ? 1 : 0);

    packet.Worldstates.emplace_back(2770, 1); // Horde top-stats (1 - show, 0 - hide) // 02 -> horde picked up the flag
    packet.Worldstates.emplace_back(2769, 1); // Alliance top-stats (1 - show, 0 - hide) // 02 -> alliance picked up the flag
    packet.Worldstates.emplace_back(2750, GetTeamScore(TEAM_HORDE)); // Horde resources
    packet.Worldstates.emplace_back(2749, GetTeamScore(TEAM_ALLIANCE)); // Alliance resources
    packet.Worldstates.emplace_back(2565, 142); // unk, constant?
    packet.Worldstates.emplace_back(2720, 0); // Capturing progress-bar (100 -> empty (only grey), 0 -> blue|red (no grey), default 0)
    packet.Worldstates.emplace_back(2719, 0); // Capturing progress-bar (0 - left, 100 - right)
    packet.Worldstates.emplace_back(2718, 0); // Capturing progress-bar (1 - show, 0 - hide)
    packet.Worldstates.emplace_back(3085, 379); // unk, constant?
}

WorldSafeLocsEntry const* BattlegroundEY::GetClosestGraveyard(Player* player)
{
    uint32 g_id = 0;

    switch (player->GetTeam())
    {
        case ALLIANCE: g_id = EY_GRAVEYARD_MAIN_ALLIANCE; break;
        case HORDE:    g_id = EY_GRAVEYARD_MAIN_HORDE;    break;
        default:       return nullptr;
    }

    float distance, nearestDistance;

    WorldSafeLocsEntry const* entry = nullptr;
    WorldSafeLocsEntry const* nearestEntry = nullptr;
    entry = sWorldSafeLocsStore.LookupEntry(g_id);
    nearestEntry = entry;

    if (!entry)
    {
        TC_LOG_ERROR("bg.battleground", "BattlegroundEY: The main team graveyard could not be found. The graveyard system will not be operational!");
        return nullptr;
    }

    float plr_x = player->GetPositionX();
    float plr_y = player->GetPositionY();
    float plr_z = player->GetPositionZ();

    distance = (entry->Loc.X - plr_x)*(entry->Loc.X - plr_x) + (entry->Loc.Y - plr_y)*(entry->Loc.Y - plr_y) + (entry->Loc.Z - plr_z)*(entry->Loc.Z - plr_z);
    nearestDistance = distance;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        if (m_PointOwnedByTeam[i] == player->GetTeam() && m_PointState[i] == EY_POINT_UNDER_CONTROL)
        {
            entry = sWorldSafeLocsStore.LookupEntry(m_CapturingPointTypes[i].GraveyardId);
            if (!entry)
                TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Graveyard {} could not be found.", m_CapturingPointTypes[i].GraveyardId);
            else
            {
                distance = (entry->Loc.X - plr_x)*(entry->Loc.X - plr_x) + (entry->Loc.Y - plr_y)*(entry->Loc.Y - plr_y) + (entry->Loc.Z - plr_z)*(entry->Loc.Z - plr_z);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestEntry = entry;
                }
            }
        }
    }

    return nearestEntry;
}

//npcbot
WorldSafeLocsEntry const* BattlegroundEY::GetClosestGraveyardForBot(WorldLocation const& curPos, uint32 team) const
{
    uint32 g_id = 0;

    switch (team)
    {
        case ALLIANCE: g_id = EY_GRAVEYARD_MAIN_ALLIANCE; break;
        case HORDE:    g_id = EY_GRAVEYARD_MAIN_HORDE;    break;
        default:       return nullptr;
    }

    float distance, nearestDistance;

    WorldSafeLocsEntry const* entry = nullptr;
    WorldSafeLocsEntry const* nearestEntry = nullptr;
    entry = sWorldSafeLocsStore.LookupEntry(g_id);
    nearestEntry = entry;

    if (!entry)
    {
        TC_LOG_ERROR("bg.battleground", "BattlegroundEY: The main team graveyard could not be found. The graveyard system will not be operational!");
        return nullptr;
    }

    float x = curPos.GetPositionX();
    float y = curPos.GetPositionY();
    float z = curPos.GetPositionZ();

    distance = (entry->Loc.X - x)*(entry->Loc.X - x) + (entry->Loc.Y - y)*(entry->Loc.Y - y) + (entry->Loc.Z - z)*(entry->Loc.Z - z);
    nearestDistance = distance;

    for (uint8 i = 0; i < EY_POINTS_MAX; ++i)
    {
        if (m_PointOwnedByTeam[i] == team && m_PointState[i] == EY_POINT_UNDER_CONTROL)
        {
            entry = sWorldSafeLocsStore.LookupEntry(m_CapturingPointTypes[i].GraveyardId);
            if (!entry)
                TC_LOG_ERROR("bg.battleground", "BattlegroundEY: Graveyard {} could not be found for bot at pos: {}", m_CapturingPointTypes[i].GraveyardId, curPos.ToString());
            else
            {
                distance = (entry->Loc.X - x)*(entry->Loc.X - x) + (entry->Loc.Y - y)*(entry->Loc.Y - y) + (entry->Loc.Z - z)*(entry->Loc.Z - z);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestEntry = entry;
                }
            }
        }
    }

    return nearestEntry;
}
//end npcbot

bool BattlegroundEY::IsAllNodesControlledByTeam(uint32 team) const
{
    uint32 count = 0;
    for (int i = 0; i < EY_POINTS_MAX; ++i)
        if (m_PointOwnedByTeam[i] == team && m_PointState[i] == EY_POINT_UNDER_CONTROL)
            ++count;

    return count == EY_POINTS_MAX;
}

uint32 BattlegroundEY::GetPrematureWinner()
{
    if (GetTeamScore(TEAM_ALLIANCE) > GetTeamScore(TEAM_HORDE))
        return ALLIANCE;
    else if (GetTeamScore(TEAM_HORDE) > GetTeamScore(TEAM_ALLIANCE))
        return HORDE;

    return Battleground::GetPrematureWinner();
}
