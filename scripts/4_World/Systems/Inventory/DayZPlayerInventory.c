/**
 * @class		DayZPlayerInventory
 **/
class DayZPlayerInventory : HumanInventoryWithFSM
{
	void DayZPlayerInventory ()
	{ }

	DayZPlayer GetDayZPlayerOwner () { return DayZPlayer.Cast(GetInventoryOwner()); }

	override void Init ()
	{
		super.Init(); // @NOTE: temporary redirection, DZP will have own fsm
	}

	///@{ juncture
	/**@fn			OnInventoryJunctureFromServer
	 * @brief		reaction to engine callback
	 *	originates in:
	 *				engine - DayZPlayer::OnSyncJuncture
	 *				script - PlayerBase.OnSyncJuncture
	 **/
	override bool OnInventoryJunctureFromServer (ParamsReadContext ctx)
	{
		int tmp = -1;
		ctx.Read(tmp);
		syncDebugPrint("[syncinv] store Juncture packet");
		StoreJunctureData(ctx);
		return true;
	}

	/**@fn			OnHandleStoredJunctureData
	 * @brief		reaction to engine callback
	 *	originates in engine - DayZPlayerInventory::HandleStoredJunctureData
	 **/
	protected void OnHandleStoredJunctureData (ParamsReadContext ctx)
	{
		int tmp = -1;
		ctx.Read(tmp);
		syncDebugPrint("[syncinv] handle JunctureData packet");
		HandleInputData(true, false, ctx);
	}

	/**@fn			StoreJunctureData
	 * @brief		stores received input user data for later handling
	/aaa
	 **/
	proto native void StoreJunctureData (ParamsReadContext ctx);
	///@} juncture


	///@{ input user data
	/**@fn			OnInputUserDataProcess
	 * @brief		reaction to engine callback
	 *	originates in:
	 *				engine - DayZPlayer::OnInputUserDataReceived
	 *				script - DayZPlayerImplement.OnInputUserDataReceived
	 **/
	override bool OnInputUserDataProcess (ParamsReadContext ctx)
	{
		syncDebugPrint("[syncinv] store InputUserData packet");
		StoreInputUserData(ctx);
		return true;
	}

	/**@fn			OnHandleStoredInputUserData
	 * @brief		reaction to engine callback
	 *	originates in engine - DayZPlayerInventory::HandleStoredInputUserData
	 **/
	protected void OnHandleStoredInputUserData (ParamsReadContext ctx)
	{
		int tmp = -1;
		ctx.Read(tmp);
		syncDebugPrint("[syncinv] handle InputUserData packet");
		HandleInputData(false, false, ctx);
	}

	/**@fn			StoreInputUserData
	 * @brief		stores received input user data for later handling
	 **/
	proto native void StoreInputUserData (ParamsReadContext ctx);
	///@} input user data
	
	bool OnInputUserDataFromRemote (ParamsReadContext ctx)
	{
		syncDebugPrint("[syncinv] remote handling InputUserData packet from server");
		return HandleInputData(false, true, ctx);
	}

	override void OnServerInventoryCommand (ParamsReadContext ctx)
	{
		syncDebugPrint("[syncinv] DZPInventory command from server");
		HandleInputData(true, true, ctx);
	}

	/**@fn			HandleInputData
	 * @brief		real processing of the input data
	 **/
	bool HandleInputData (bool juncture, bool remote, ParamsReadContext ctx)
	{
		int type = -1;
		if (!ctx.Read(type))
			return false;

		InventoryLocation src = new InventoryLocation;
		InventoryLocation dst = new InventoryLocation;

		switch (type)
		{
			case InventoryCommandType.SYNC_MOVE:
			{
				src.ReadFromContext(ctx);
				dst.ReadFromContext(ctx);
				syncDebugPrint("[syncinv] t=" + GetGame().GetTime() + "ms received cmd=" + typename.EnumToString(InventoryCommandType, type) + " src=" + src.DumpToString() + " dst=" + dst.DumpToString());

				if (remote && (!src.GetItem() || !dst.GetItem()))
				{
					syncDebugPrint("[syncinv] remote input (cmd=SYNC_MOVE) dropped, item not in bubble");
					break; // not in bubble
				}

				if (!juncture && GetDayZPlayerOwner().GetInstanceType() == DayZPlayerInstanceType.INSTANCETYPE_SERVER)
				{
					if (GetDayZPlayerOwner().NeedInventoryJunctureFromServer(src.GetItem(), src.GetParent(), dst.GetParent()))
					{
						syncDebugPrint("[syncinv] redir to juncture cmd=" + typename.EnumToString(InventoryCommandType, type) + " dst=" + dst.DumpToString());
						if (GetGame().AddInventoryJuncture(GetDayZPlayerOwner(), src.GetItem(), dst, true, GameInventory.c_InventoryReservationTimeoutMS))
							GetDayZPlayerOwner().SendSyncJuncture(DayZPlayerSyncJunctures.SJ_INVENTORY, ctx);
						else
						{
							syncDebugPrint("[syncinv] redir to juncture DENIED");
							return true; // abort, do not send anything to remotes
						}
						break;
					}
				}

				LocationSyncMoveEntity(src, dst);
				break;
			}

			case InventoryCommandType.HAND_EVENT:
			{
				HandEventBase e = HandEventBase.CreateHandEventFromContext(ctx);
				syncDebugPrint("[syncinv] t=" + GetGame().GetTime() + "ms received cmd=" + typename.EnumToString(InventoryCommandType, type) + " event=" + e);

				if (remote && !e.m_Entity)
				{
					syncDebugPrint("[syncinv] remote input (cmd=HAND_EVENT) dropped, item not in bubble");
					break; // not in bubble
				}

				if (e.m_Entity.GetInventory().GetCurrentInventoryLocation(src))
				{
					dst = e.GetDst();
					if (!juncture && GetDayZPlayerOwner().GetInstanceType() == DayZPlayerInstanceType.INSTANCETYPE_SERVER)
					{
						if (GetDayZPlayerOwner().NeedInventoryJunctureFromServer(src.GetItem(), src.GetParent(), dst.GetParent()))
						{
							syncDebugPrint("[syncinv] redir to juncture cmd=" + typename.EnumToString(InventoryCommandType, type) + " event=" + e);
							bool test_dst_occupancy = !e.IsSwapEvent();
							if (GetGame().AddInventoryJuncture(GetDayZPlayerOwner(), e.m_Entity, dst, test_dst_occupancy, GameInventory.c_InventoryReservationTimeoutMS))
								GetDayZPlayerOwner().SendSyncJuncture(DayZPlayerSyncJunctures.SJ_INVENTORY, ctx);
							else
							{
								syncDebugPrint("[syncinv] redir to juncture DENIED");
								return true; // abort, do not send anything to remotes
							}
							break;
						}
					}

					e.m_Player.PostHandEvent(e);
				}
				else
					Error("OnInputData HandEvent - no inv loc");
				break;
			}

			case InventoryCommandType.SWAP:
			{
				EntityAI item1, item2;
				ctx.Read(item1);
				ctx.Read(item2);
				
				if (remote && (!item1 || !item2))
				{
					syncDebugPrint("[syncinv] remote input (cmd=SWAP) dropped, item not in bubble");
					break; // not in bubble
				}

				// workaround for commented code
				InventoryLocation src1, src2, dst1, dst2;
				if (GameInventory.MakeSrcAndDstForSwap(item1, item2, src1, src2, dst1, dst2))
				{
					dst1.Copy(src1);
					dst1.CopyLocationFrom(src2);
					dst2.Copy(src2);
					dst2.CopyLocationFrom(src1);
					syncDebugPrint("[syncinv] t=" + GetGame().GetTime() + "ms received Swap src1=" + src1.DumpToString() + "dst1=" + dst1.DumpToString() +  "src2=" + src2.DumpToString() + "dst2=" + dst2.DumpToString());

					if (!juncture && GetDayZPlayerOwner().GetInstanceType() == DayZPlayerInstanceType.INSTANCETYPE_SERVER)
					{
						bool need_j1 = GetDayZPlayerOwner().NeedInventoryJunctureFromServer(src1.GetItem(), src1.GetParent(), dst1.GetParent());
						bool need_j2 = GetDayZPlayerOwner().NeedInventoryJunctureFromServer(src2.GetItem(), src2.GetParent(), dst2.GetParent());
						if (need_j1 || need_j2)
						{
							syncDebugPrint("[syncinv] redir to juncture cmd=" + typename.EnumToString(InventoryCommandType, type) + " dst1=" + dst1.DumpToString());
							bool acq_j1 = GetGame().AddInventoryJuncture(GetDayZPlayerOwner(), item1, dst1, false, GameInventory.c_InventoryReservationTimeoutMS);
							bool acq_j2 = GetGame().AddInventoryJuncture(GetDayZPlayerOwner(), item2, dst2, false, GameInventory.c_InventoryReservationTimeoutMS);
							if (acq_j1 && acq_j2)
								GetDayZPlayerOwner().SendSyncJuncture(DayZPlayerSyncJunctures.SJ_INVENTORY, ctx);
							else
							{
								syncDebugPrint("[syncinv] redir to juncture DENIED");
								return true; // abort, do not send anything to remotes
							}
							break;
						}
					}

					LocationSwap(src1, src2, dst1, dst2);
				}
				else
					Error("MakeSrcAndDstForSwap - no inv loc");
				break;
			}

			case InventoryCommandType.FORCESWAP:
			{
				Error("[syncinv] ForceSwap TODO");
				break;
			}

			case InventoryCommandType.DESTROY:
			{
				src.ReadFromContext(ctx);
				syncDebugPrint("[syncinv] t=" + GetGame().GetTime() + "ms received cmd=" + typename.EnumToString(InventoryCommandType, type) + " src=" + src.DumpToString());

				if (remote && !src.GetItem())
				{
					syncDebugPrint("[syncinv] remote input (cmd=DESTROY) dropped, item not in bubble");
					break; // not in bubble
				}

				GetGame().ObjectDelete(src.GetItem());
				break;
			}
		}

		if (!juncture && !remote && GetDayZPlayerOwner().GetInstanceType() == DayZPlayerInstanceType.INSTANCETYPE_SERVER)
		{
			syncDebugPrint("[syncinv] forward to remotes cmd=" + typename.EnumToString(InventoryCommandType, type));
			GetDayZPlayerOwner().StoreInputForRemotes(ctx); // @NOTE: needs to be called _after_ the operation
		}

		return true;
	}

	/**@fn			NetSyncCurrentStateID
	 * @brief		Engine callback - network synchronization of FSM's state. not intended to direct use.
	 **/
	override void NetSyncCurrentStateID (int id)
	{
		super.NetSyncCurrentStateID(id);

		GetDayZPlayerOwner().GetItemAccessor().OnItemInHandsChanged();
	}

	/**@fn			OnAfterStoreLoad
	 * @brief		engine reaction to load from database
	 *	originates in:
	 *				engine - Person::BinLoad
	 *				script - PlayerBase.OnAfterStoreLoad
	 **/
	override void OnAfterStoreLoad ()
	{
		GetDayZPlayerOwner().GetItemAccessor().OnItemInHandsChanged();
	}

	/**@fn			OnEventFromRemoteWeapon
	 * @brief		reaction of remote weapon to (common user/anim/...) event sent from server
	 **/
	bool OnEventFromRemoteWeapon (ParamsReadContext ctx)
	{
		if (GetEntityInHands())
		{
			Weapon_Base wpn = Weapon_Base.Cast(GetEntityInHands());
			if (wpn)
			{
				wpn.OnEventFromRemote(ctx);
			}
			else
				Error("OnEventFromRemoteWeapon - entity in hands, but not weapon. item=" + GetEntityInHands());
		}
		else
			Error("OnEventFromRemoteWeapon - no entity in hands");
		return true;
	}

	/**@fn			OnSyncFromRemoteWeapon
	 * @brief		stable state synchronization from weapon on server
	 **/
	bool OnSyncFromRemoteWeapon (ParamsReadContext ctx)
	{
		syncDebugPrint("[syncinv] remote weapon handling of stable-sync-event from server");

		if (GetEntityInHands())
		{
			Weapon_Base wpn = Weapon_Base.Cast(GetEntityInHands());
			if (wpn)
			{
				wpn.OnSyncFromRemote(ctx);
			}
			else
				Error("OnSyncFromRemoteWeapon - entity in hands, but not weapon. item=" + GetEntityInHands());
		}
		else
			Error("OnSyncFromRemoteWeapon - no entity in hands");
		return true;
	}
};

