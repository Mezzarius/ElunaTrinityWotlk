#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "TaxiAction.h"
#include "../values/LastMovementValue.h"
#include "WorldSession.h"
#include "Creature.h"
#include "FlightPathMovementGenerator.h"
using namespace ai;

bool TaxiAction::Execute(Event event)
{
    ai->RemoveShapeshift();

    LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();

    WorldPacket& p = event.getPacket();
    if (!p.empty() && p.GetOpcode() == CMSG_MOVE_SPLINE_DONE)
    {
		uint32 curDest = ai->GetBot()->m_taxi.GetTaxiDestination();
		if (curDest)
		{
			TaxiNodesEntry const* curDestNode = sTaxiNodesStore.LookupEntry(curDest);

			// far teleport case
			if (curDestNode && curDestNode->map_id != ai->GetBot()->GetMapId())
			{
				if (ai->GetBot()->GetMotionMaster()->GetCurrentMovementGeneratorType() == FLIGHT_MOTION_TYPE)
				{
					// short preparations to continue flight
					FlightPathMovementGenerator* flight = (FlightPathMovementGenerator*)(ai->GetBot()->GetMotionMaster()->GetCurrentMovementGenerator());

					flight->SetCurrentNodeAfterTeleport();
					TaxiPathNodeEntry const* node = flight->GetPath()[flight->GetCurrentNode()];
					flight->SkipCurrentNode();

					ai->GetBot()->TeleportTo(curDestNode->map_id, node->LocX, node->LocY, node->LocZ, ai->GetBot()->GetOrientation());
				}
			}

			return true;
		}

		// at this point only 1 node is expected (final destination)
		if (ai->GetBot()->m_taxi.GetPath().size() != 1)
			return false;

		ai->GetBot()->CleanupAfterTaxiFlight();
		ai->GetBot()->SetFallInformation(0, ai->GetBot()->GetPositionZ());
		if (ai->GetBot()->pvpInfo.IsHostile)
			ai->GetBot()->CastSpell(ai->GetBot(), 2479, true);


/*		WorldPacket p1(p);
        p1.rpos(0);
        bot->GetSession()->HandleMoveSplineDoneOpcode(p1);
        movement.taxiNodes.clear();
        movement.Set(NULL); */
        return true;
    }

    list<ObjectGuid> units = *context->GetValue<list<ObjectGuid> >("nearest npcs");
    for (list<ObjectGuid>::iterator i = units.begin(); i != units.end(); i++)
    {
        Creature *npc = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_FLIGHTMASTER);
        if (!npc)
            continue;

        if (movement.taxiNodes.empty())
        {
            ostringstream out;
            out << "I will order the taxi from " << npc->GetName() << ". Please start flying, then instruct me again";
            ai->TellMaster(out);
            return true;
        }

        if (!bot->ActivateTaxiPathTo(movement.taxiNodes, npc))
        {
            ai->TellMaster("I can't fly with you");
            return false;
        }

        return true;
    }

    ai->TellMaster("Cannot find any flightmaster to talk");
    return false;
}
