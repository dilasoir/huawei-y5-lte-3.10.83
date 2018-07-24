/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/windows/common/gl_kal_common.c#2 $
*/

/*! \file   gl_kal_common.c
    \brief  KAL routines of Windows driver

*/





/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "config.h"
#include "gl_os.h"
#include "mac.h"
#include "link.h"
#include "wlan_def.h"
#include "cmd_buf.h"
#include "mt6630_reg.h"

#include <ntddk.h>
#include "Ntstrsafe.h"
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
ULONG RtlRandomEx(__inout PULONG Seed);

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


/*----------------------------------------------------------------------------*/
/*!
* @brief Cache-able memory free
*
* @param pvAddr Starting address of the memory
* @param eMemType Memory allocation type
* @param u4Size Size of memory to allocate, in bytes
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID kalMemFree(IN PVOID pvAddr, IN ENUM_KAL_MEM_ALLOCATION_TYPE eMemType, IN UINT_32 u4Size)
{
	NdisFreeMemory(pvAddr, u4Size, 0);
}				/* kalMemFree */


/*----------------------------------------------------------------------------*/
/*!
* @brief Micro-second level delay function
*
* @param u4MicroSec Number of microsecond to delay
*
* @retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalUdelay(IN UINT_32 u4MicroSec)
{
	/* for WinCE 4.2 kernal, NdisStallExecution() will cause a known
	   issue that it will delay over the input value */
	while (u4MicroSec > 50) {
		NdisStallExecution(50);
		u4MicroSec -= 50;
	}

	if (u4MicroSec > 0) {
		NdisStallExecution(u4MicroSec);
	}
}				/* kalUdelay */


/*----------------------------------------------------------------------------*/
/*!
* @brief Milli-second level delay function
*
* @param u4MilliSec Number of millisecond to delay
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID kalMdelay(IN UINT_32 u4MilliSec)
{
	kalUdelay(u4MilliSec * 1000);
}				/* kalMdelay */


/*----------------------------------------------------------------------------*/
/*!
* @brief Milli-second level sleep function
*
* @param u4MilliSec Number of millisecond to sleep
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID kalMsleep(IN UINT_32 u4MilliSec)
{
	UINT_32 u4Timestamp[2], u4RemainTime;

	u4RemainTime = u4MilliSec;

	/* NdisMSleep is not accurate and might be sleeping for less than expected */
	while (u4RemainTime > 0) {
		u4Timestamp[0] = SYSTIME_TO_MSEC(kalGetTimeTick());

		NdisMSleep(u4MilliSec);

		u4Timestamp[1] = SYSTIME_TO_MSEC(kalGetTimeTick());

		/* to avoid system tick wraps around */
		if (u4Timestamp[1] >= u4Timestamp[0]) {
			if ((u4Timestamp[1] - u4Timestamp[0]) >= u4RemainTime) {
				break;
			} else {
				u4RemainTime -= (u4Timestamp[1] - u4Timestamp[0]);
			}
		} else {
			if ((u4Timestamp[1] + (UINT_MAX - u4Timestamp[0])) >= u4RemainTime) {
				break;
			} else {
				u4RemainTime -= (u4Timestamp[1] + (UINT_MAX - u4Timestamp[0]));
			}
		}
	}
}				/* kalMsleep */

/*----------------------------------------------------------------------------*/
/*!
* @brief This function sets a given event to the Signaled state if it was not already signaled.
*
* @param prGlueInfo     Pointer of GLUE Data Structure
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID kalSetEvent(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Set EVENT */
	NdisSetEvent(&prGlueInfo->rTxReqEvent);

}				/* end of kalSetEvent() */


/*----------------------------------------------------------------------------*/
/*!
* @brief Notify Host the Interrupt serive is done, used at Indigo SC32442 SP, the ISR call
*         only one time issue
*
* @param prGlueInfo     Pointer of GLUE Data Structure
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID kalInterruptDone(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

#if SC32442_SPI && 0
	InterruptDone(prGlueInfo->rHifInfo.u4sysIntr);
#endif
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Thread to process TX request and CMD request
*
* @param pvGlueContext     Pointer of GLUE Data Structure
*
* @retval WLAN_STATUS_SUCCESS.
* @retval WLAN_STATUS_FAILURE.
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS kalTxServiceThread(IN PVOID pvGlueContext)
{
	NDIS_STATUS ndisStatus = NDIS_STATUS_SUCCESS;
	P_GLUE_INFO_T prGlueInfo;
	P_QUE_ENTRY_T prQueueEntry;
	P_QUE_T prTxQue;
	P_QUE_T prCmdQue;
	P_QUE_T prReturnQueue;
	PNDIS_PACKET prNdisPacket;
	P_GL_HIF_INFO_T prHifInfo = NULL;
	BOOLEAN fgNeedHwAccess;
	GLUE_SPIN_LOCK_DECLARATION();


	ASSERT(pvGlueContext);

	prGlueInfo = (P_GLUE_INFO_T) pvGlueContext;
	prTxQue = &prGlueInfo->rTxQueue;
	prCmdQue = &prGlueInfo->rCmdQueue;
	prReturnQueue = &prGlueInfo->rReturnQueue;
	prHifInfo = &prGlueInfo->rHifInfo;

	/* CeSetThreadPriority(GetCurrentThread(),100); */

	DEBUGFUNC("kalTxServiceThread");
/* DBGLOG(INIT, TRACE, ("\n")); */

	KeSetEvent(&prHifInfo->rOidReqEvent, EVENT_INCREMENT, FALSE);

	while (TRUE) {
		if (GLUE_TEST_FLAG(prGlueInfo, GLUE_FLAG_HALT)) {
			DBGLOG(INIT, TRACE, ("kalTxServiceThread - GLUE_FLAG_HALT!!\n"));
			break;
		}

		GLUE_WAIT_EVENT(prGlueInfo);
		GLUE_RESET_EVENT(prGlueInfo);

		/* Remove pending OID and TX for RESET */
		if (GLUE_TEST_FLAG(prGlueInfo, GLUE_FLAG_RESET)) {
			if (prGlueInfo->i4OidPendingCount) {
				/* Call following function will remove one pending OID */
				wlanReleasePendingOid(prGlueInfo->prAdapter, 0);
			}

			/* Remove pending TX */
			if (prGlueInfo->i4TxPendingFrameNum) {
				kalFlushPendingTxPackets(prGlueInfo);

				wlanFlushTxPendingPackets(prGlueInfo->prAdapter);
			}
			/* Remove pending security frames */
			if (prGlueInfo->i4TxPendingSecurityFrameNum) {
				kalClearSecurityFrames(prGlueInfo);
			}

			if ((!prGlueInfo->i4OidPendingCount) &&
			    (!prGlueInfo->i4TxPendingFrameNum) &&
			    (!prGlueInfo->i4TxPendingSecurityFrameNum)) {

				GLUE_CLEAR_FLAG(prGlueInfo, GLUE_FLAG_RESET);

				DBGLOG(INIT, WARN, ("NdisMResetComplete()\n"));
				NdisMResetComplete(prGlueInfo->rMiniportAdapterHandle,
						   NDIS_STATUS_SUCCESS, FALSE);
			}
		}

		fgNeedHwAccess = FALSE;

#if CFG_SUPPORT_SDIO_READ_WRITE_PATTERN
		if (prGlueInfo->fgEnSdioTestPattern == TRUE) {
			if (fgNeedHwAccess == FALSE) {
				fgNeedHwAccess = TRUE;

				wlanAcquirePowerControl(prGlueInfo->prAdapter);
			}

			if (prGlueInfo->fgIsSdioTestInitialized == FALSE) {
				/* enable PRBS mode */
				kalDevRegWrite(prGlueInfo, MCR_WTMCR, 0x00080002);
				prGlueInfo->fgIsSdioTestInitialized = TRUE;
			}

			if (prGlueInfo->fgSdioReadWriteMode == TRUE) {
				/* read test */
				kalDevPortRead(prGlueInfo,
					       MCR_WTMDR,
					       256,
					       prGlueInfo->aucSdioTestBuffer,
					       sizeof(prGlueInfo->aucSdioTestBuffer));
			} else {
				/* write test */
				kalDevPortWrite(prGlueInfo,
						MCR_WTMDR,
						172,
						prGlueInfo->aucSdioTestBuffer,
						sizeof(prGlueInfo->aucSdioTestBuffer));
			}
		}
#endif

		/* Process Mailbox Messages */
		wlanProcessMboxMessage(prGlueInfo->prAdapter);

		/* Process CMD request */
		if (wlanGetAcpiState(prGlueInfo->prAdapter) == ACPI_STATE_D0
		    && prCmdQue->u4NumElem > 0) {
			/* Acquire LP-OWN */
			if (fgNeedHwAccess == FALSE) {
				fgNeedHwAccess = TRUE;

				wlanAcquirePowerControl(prGlueInfo->prAdapter);
			}

			wlanProcessCommandQueue(prGlueInfo->prAdapter, prCmdQue);
		}

		/* Process TX request */
		{
			/* <0> packets from OS handling */
			while (TRUE) {
				GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);
				QUEUE_REMOVE_HEAD(prTxQue, prQueueEntry, P_QUE_ENTRY_T);
				GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);

				if (prQueueEntry == NULL) {
					break;
				}

				prNdisPacket = GLUE_GET_PKT_DESCRIPTOR(prQueueEntry);

				if (wlanEnqueueTxPacket(prGlueInfo->prAdapter,
							(P_NATIVE_PACKET) prNdisPacket) ==
				    WLAN_STATUS_RESOURCES) {
					GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);
					QUEUE_INSERT_HEAD(prTxQue, prQueueEntry);
					GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);

					break;
				}
			}

			if (wlanGetTxPendingFrameCount(prGlueInfo->prAdapter) > 0) {
				wlanTxPendingPackets(prGlueInfo->prAdapter, &fgNeedHwAccess);
			}
		}

		/* Process RX request */
		{
			UINT_32 u4RegValue;
			UINT_32 u4CurrAvailFreeRfbCnt;

			while (TRUE) {

				GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_RX_RETURN_QUE);
				QUEUE_REMOVE_HEAD(prReturnQueue, prQueueEntry, P_QUE_ENTRY_T);
				GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_RX_RETURN_QUE);

				if (prQueueEntry == NULL) {
					break;
				}

				prNdisPacket = MP_GET_MR_PKT(prQueueEntry);

				wlanReturnPacket(prGlueInfo->prAdapter, (PVOID) prNdisPacket);
			}
		}

		/* Process OID request */
		{
			P_WLAN_REQ_ENTRY prEntry;
			UINT_32 u4QueryInfoLen;
			UINT_32 u4SetInfoLen;
			KIRQL rOldIrql;
			WLAN_STATUS rStatus;

			if (_InterlockedAnd(&prGlueInfo->rHifInfo.u4ReqFlag, ~REQ_FLAG_OID) &
			    REQ_FLAG_OID) {

				prEntry = (P_WLAN_REQ_ENTRY) prGlueInfo->pvOidEntry;

				if (prGlueInfo->fgSetOid) {
					if (prGlueInfo->fgIsGlueExtension) {
						rStatus = prEntry->pfOidSetHandler(prGlueInfo,
										   prGlueInfo->
										   pvInformationBuffer,
										   prGlueInfo->
										   u4InformationBufferLength,
										   &u4SetInfoLen);
					} else {
						rStatus = wlanSetInformation(prGlueInfo->prAdapter,
									     prEntry->
									     pfOidSetHandler,
									     prGlueInfo->
									     pvInformationBuffer,
									     prGlueInfo->
									     u4InformationBufferLength,
									     &u4SetInfoLen);
					}

					/* 20091012 George */
					/* it seems original code does not properly handle the case
					 * which there's command/ response to be handled within OID handler.
					 * it should wait until command/ response done and then indicate completed to NDIS.
					 *
					 */
					if (prGlueInfo->pu4BytesReadOrWritten)
						*prGlueInfo->pu4BytesReadOrWritten = u4SetInfoLen;

					if (prGlueInfo->pu4BytesNeeded)
						*prGlueInfo->pu4BytesNeeded = u4SetInfoLen;

					if (rStatus != WLAN_STATUS_PENDING) {
						KeRaiseIrql(DISPATCH_LEVEL, &rOldIrql);


						GLUE_DEC_REF_CNT(prGlueInfo->i4OidPendingCount);
						ASSERT(prGlueInfo->i4OidPendingCount == 0);

						NdisMSetInformationComplete(prGlueInfo->
									    rMiniportAdapterHandle,
									    rStatus);
						KeLowerIrql(rOldIrql);
					} else {
						wlanoidTimeoutCheck(prGlueInfo->prAdapter,
								    prEntry->pfOidSetHandler);
					}
				} else {
					if (prGlueInfo->fgIsGlueExtension) {
						rStatus = prEntry->pfOidQueryHandler(prGlueInfo,
										     prGlueInfo->
										     pvInformationBuffer,
										     prGlueInfo->
										     u4InformationBufferLength,
										     &u4QueryInfoLen);
					} else {
						rStatus =
						    wlanQueryInformation(prGlueInfo->prAdapter,
									 prEntry->pfOidQueryHandler,
									 prGlueInfo->
									 pvInformationBuffer,
									 prGlueInfo->
									 u4InformationBufferLength,
									 &u4QueryInfoLen);
					}

					if (prGlueInfo->pu4BytesReadOrWritten) {
						/* This case is added to solve the problem of both 32-bit
						 * and 64-bit counters supported for the general
						 * statistics OIDs. */
						if (u4QueryInfoLen >
						    prGlueInfo->u4InformationBufferLength) {
							*prGlueInfo->pu4BytesReadOrWritten =
							    prGlueInfo->u4InformationBufferLength;
						} else {
							*prGlueInfo->pu4BytesReadOrWritten =
							    u4QueryInfoLen;
						}
					}

					if (prGlueInfo->pu4BytesNeeded)
						*prGlueInfo->pu4BytesNeeded = u4QueryInfoLen;

					if (rStatus != WLAN_STATUS_PENDING) {
						KeRaiseIrql(DISPATCH_LEVEL, &rOldIrql);


						GLUE_DEC_REF_CNT(prGlueInfo->i4OidPendingCount);
						ASSERT(prGlueInfo->i4OidPendingCount == 0);

						NdisMQueryInformationComplete(prGlueInfo->
									      rMiniportAdapterHandle,
									      rStatus);
						KeLowerIrql(rOldIrql);
					} else {
						wlanoidTimeoutCheck(prGlueInfo->prAdapter,
								    prEntry->pfOidSetHandler);
					}
				}
			}
		}

		/* TODO(Kevin): Should change to test & reset */
		if (_InterlockedAnd(&prGlueInfo->rHifInfo.u4ReqFlag, ~REQ_FLAG_INT) & REQ_FLAG_INT) {
			if (wlanGetAcpiState(prGlueInfo->prAdapter) == ACPI_STATE_D0) {
				/* Acquire LP-OWN */
				if (fgNeedHwAccess == FALSE) {
					fgNeedHwAccess = TRUE;

					wlanAcquirePowerControl(prGlueInfo->prAdapter);
				}

				wlanIST(prGlueInfo->prAdapter);
			}
		}

		if (wlanGetAcpiState(prGlueInfo->prAdapter) == ACPI_STATE_D0
		    && fgNeedHwAccess == TRUE) {
			wlanReleasePowerControl(prGlueInfo->prAdapter);
		}

		/* Do Reset if no pending OID and TX */
		if (GLUE_TEST_FLAG(prGlueInfo, GLUE_FLAG_RESET) &&
		    (!prGlueInfo->i4OidPendingCount) &&
		    (!prGlueInfo->i4TxPendingFrameNum) &&
		    (!prGlueInfo->i4TxPendingSecurityFrameNum)) {

			GLUE_CLEAR_FLAG(prGlueInfo, GLUE_FLAG_RESET);

			DBGLOG(INIT, WARN, ("NdisMResetComplete()\n"));
			NdisMResetComplete(prGlueInfo->rMiniportAdapterHandle,
					   NDIS_STATUS_SUCCESS, FALSE);
		}

		if (GLUE_TEST_FLAG(prGlueInfo, GLUE_FLAG_TIMEOUT)) {
			GLUE_CLEAR_FLAG(prGlueInfo, GLUE_FLAG_TIMEOUT);

			wlanTimerTimeoutCheck(prGlueInfo->prAdapter);
		}
#if CFG_SUPPORT_SDIO_READ_WRITE_PATTERN
		if (prGlueInfo->fgEnSdioTestPattern == TRUE) {
			GLUE_SET_EVENT(prGlueInfo);
		}
#endif
	}

	while (TRUE) {
		GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_RX_RETURN_QUE);
		QUEUE_REMOVE_HEAD(prReturnQueue, prQueueEntry, P_QUE_ENTRY_T);
		GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_RX_RETURN_QUE);

		if (prQueueEntry == NULL) {

			if (prGlueInfo->i4RxPendingFrameNum) {
				/* DbgPrint("Wait for RX return, left = %d\n", prGlueInfo->u4RxPendingFrameNum); */
				NdisMSleep(100000);	/* Sleep 100ms for remaining RX Packets */
			} else {
				/* No pending RX Packet */
				break;
			}
		} else {
			prNdisPacket = MP_GET_MR_PKT(prQueueEntry);

			wlanReturnPacket(prGlueInfo->prAdapter, (PVOID) prNdisPacket);
		}
	}

	return ndisStatus;
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Download the patch file
*
* @param
*
* @retval WLAN_STATUS_SUCCESS: SUCCESS
* @retval WLAN_STATUS_FAILURE: FAILURE
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS kalDownloadPatch(IN PNDIS_STRING FileName)
{
	NDIS_STATUS rStatus;
	NDIS_HANDLE FileHandle;
	UINT_32 u4FileLength;
	NDIS_PHYSICAL_ADDRESS HighestAcceptableAddress = NDIS_PHYSICAL_ADDRESS_CONST(-1, -1);
	PUINT_8 pucMappedBuffer;

	/* Open the patch file. */
	NdisOpenFile(&rStatus, &FileHandle, &u4FileLength, FileName, HighestAcceptableAddress);

	if (rStatus == NDIS_STATUS_SUCCESS) {
		/* Map the firmware file to memory. */
		NdisMapFile(&rStatus, &pucMappedBuffer, FileHandle);

		if (rStatus == NDIS_STATUS_SUCCESS) {
			/* Issue the download CMD */
		}

		NdisCloseFile(FileHandle);
	}

	return (rStatus);
}


/*----------------------------------------------------------------------------*/
/*!
* @brief Thread to process TX request and CMD request
*
* @param pvGlueContext     Pointer of GLUE Data Structure
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
kalOidComplete(IN P_GLUE_INFO_T prGlueInfo,
	       IN BOOLEAN fgSetQuery, IN UINT_32 u4SetQueryInfoLen, IN WLAN_STATUS rOidStatus)
{
	/* KIRQL rOldIrql; */

	ASSERT(prGlueInfo);
	ASSERT((((fgSetQuery) && (prGlueInfo->fgSetOid)) ||
		((!fgSetQuery) && (!prGlueInfo->fgSetOid))));

	/* Remove Timeout check timer */
	wlanoidClearTimeoutCheck(prGlueInfo->prAdapter);

	/* NOTE(Kevin): We should update reference count before do NdisMSet/QueryComplete()
	 * Because OS may issue new OID request when we call NdisMSet/QueryComplete().
	 */
	GLUE_DEC_REF_CNT(prGlueInfo->i4OidPendingCount);
	ASSERT(prGlueInfo->i4OidPendingCount == 0);

	/* KeRaiseIrql(DISPATCH_LEVEL, &rOldIrql); */
	if (prGlueInfo->fgSetOid) {
		if (prGlueInfo->pu4BytesReadOrWritten) {
			*prGlueInfo->pu4BytesReadOrWritten = u4SetQueryInfoLen;
		}

		if (prGlueInfo->pu4BytesNeeded) {
			*prGlueInfo->pu4BytesNeeded = u4SetQueryInfoLen;
		}

		NdisMSetInformationComplete(prGlueInfo->rMiniportAdapterHandle, rOidStatus);
	} else {
		/* This case is added to solve the problem of both 32-bit
		   and 64-bit counters supported for the general
		   statistics OIDs. */
		if (u4SetQueryInfoLen > prGlueInfo->u4InformationBufferLength) {
			u4SetQueryInfoLen = prGlueInfo->u4InformationBufferLength;
		}

		if (prGlueInfo->pu4BytesReadOrWritten) {
			*prGlueInfo->pu4BytesReadOrWritten = u4SetQueryInfoLen;
		}

		if (prGlueInfo->pu4BytesNeeded) {
			*prGlueInfo->pu4BytesNeeded = u4SetQueryInfoLen;
		}

		NdisMQueryInformationComplete(prGlueInfo->rMiniportAdapterHandle, rOidStatus);
	}
	/* KeLowerIrql(rOldIrql); */

	return;
}

/*----------------------------------------------------------------------------*/
/*!
* @brief This inline function is to extract some packet information, including
*        user priority, packet length, destination address, 802.1x and BT over Wi-Fi
*        or not.
*
* @param prGlueInfo         Pointer to the glue structure
* @param prNdisPacket       Packet descriptor
* @param pucPriorityParam   User priority
* @param pu4PacketLen       Packet length
* @param pucEthDestAddr     Destination address
* @param pfgIs1X            802.1x packet or not
* @param pfgIsPAL           BT over Wi-Fi packet or not
* @param pfgIs802_3         802.3 format packet or not
* @param pfgIsVlanExists    VLAN tagged packet or not
*
* @retval TRUE      Success to extract information
* @retval FALSE     Fail to extract correct information
*/
/*----------------------------------------------------------------------------*/
BOOL
kalQoSFrameClassifierAndPacketInfo(IN P_GLUE_INFO_T prGlueInfo,
				   IN P_NATIVE_PACKET prPacket,
				   OUT PUINT_8 pucPriorityParam,
				   OUT PUINT_32 pu4PacketLen,
				   OUT PUINT_8 pucEthDestAddr,
				   OUT PBOOLEAN pfgIs1X,
				   OUT PBOOLEAN pfgIsPAL,
				   OUT PBOOLEAN pfgIs802_3, OUT PBOOLEAN pfgIsVlanExists)
{
	PNDIS_BUFFER prNdisBuffer;
	UINT_32 u4PacketLen;

	UINT_8 aucLookAheadBuf[LOOK_AHEAD_LEN];
	UINT_32 u4ByteCount = 0;
	UINT_8 ucUserPriority = 0;	/* Default, normally glue layer cannot see into non-WLAN header files QM_DEFAULT_USER_PRIORITY; */
	UINT_8 ucLookAheadLen;
	UINT_16 u2EtherTypeLen;
	PNDIS_PACKET prNdisPacket = (PNDIS_PACKET) prPacket;

	DEBUGFUNC("kalQoSFrameClassifierAndPacketInfo");

	/* 4 <1> Find out all buffer information */
	NdisQueryPacket(prNdisPacket, NULL, NULL, &prNdisBuffer, &u4PacketLen);

	if (u4PacketLen < ETHER_HEADER_LEN) {
		DBGLOG(INIT, WARN, ("Invalid Ether packet length: %d\n", u4PacketLen));
		return FALSE;
	}

	if (u4PacketLen < LOOK_AHEAD_LEN) {
		ucLookAheadLen = (UINT_8) u4PacketLen;
	} else {
		ucLookAheadLen = LOOK_AHEAD_LEN;
	}

	/* 4 <2> Copy partial frame to local LOOK AHEAD Buffer */
	while (prNdisBuffer && u4ByteCount < ucLookAheadLen) {
		PVOID pvAddr;
		UINT_32 u4Len;
		PNDIS_BUFFER prNextNdisBuffer;

#ifdef NDIS51_MINIPORT
		NdisQueryBufferSafe(prNdisBuffer, &pvAddr, &u4Len, HighPagePriority);
#else
		NdisQueryBuffer(prNdisBuffer, &pvAddr, &u4Len);
#endif

		if (pvAddr == (PVOID) NULL) {
			ASSERT(0);
			return FALSE;
		}

		if ((u4ByteCount + u4Len) >= ucLookAheadLen) {
			kalMemCopy(&aucLookAheadBuf[u4ByteCount],
				   pvAddr, (ucLookAheadLen - u4ByteCount));
			break;
		} else {
			kalMemCopy(&aucLookAheadBuf[u4ByteCount], pvAddr, u4Len);
		}
		u4ByteCount += u4Len;

		NdisGetNextBuffer(prNdisBuffer, &prNextNdisBuffer);

		prNdisBuffer = prNextNdisBuffer;
	}

	*pfgIs1X = FALSE;
	*pfgIsPAL = FALSE;
	*pfgIsVlanExists = FALSE;
	*pfgIs802_3 = FALSE;

	/* 4 TODO: Check VLAN Tagging */

	/* 4 <3> Obtain the User Priority for WMM */
	u2EtherTypeLen = NTOHS(*(PUINT_16) & aucLookAheadBuf[ETHER_TYPE_LEN_OFFSET]);

	if ((u2EtherTypeLen == ETH_P_IPV4) && (u4PacketLen >= LOOK_AHEAD_LEN)) {
		PUINT_8 pucIpHdr = &aucLookAheadBuf[ETHER_HEADER_LEN];
		UINT_8 ucIpVersion;


		ucIpVersion = (pucIpHdr[0] & IP_VERSION_MASK) >> IP_VERSION_OFFSET;

		if (ucIpVersion == IP_VERSION_4) {
			UINT_8 ucIpTos;


			/* Get the DSCP value from the header of IP packet. */
			ucIpTos = pucIpHdr[1];
			ucUserPriority =
			    ((ucIpTos & IPV4_HDR_TOS_PREC_MASK) >> IPV4_HDR_TOS_PREC_OFFSET);
		}

		/* TODO(Kevin): Add TSPEC classifier here */
	} else if (u2EtherTypeLen == ETH_P_1X) {	/* For Port Control */
		/* DBGLOG(REQ, TRACE, ("Tx 1x\n")); */
		*pfgIs1X = TRUE;
	} else if (u2EtherTypeLen == ETH_P_PRE_1X) {	/* For Port Control */
		/* DBGLOG(REQ, TRACE, ("Tx 1x\n")); */
		*pfgIs1X = TRUE;
	}
#if CFG_SUPPORT_WAPI
	else if (u2EtherTypeLen == ETH_WPI_1X) {
		*pfgIs1X = TRUE;
	}
#endif
	else if (u2EtherTypeLen <= 1500) {	/* 802.3 Frame */
		UINT_8 ucDSAP, ucSSAP, ucControl;
		UINT_8 aucOUI[3];

		*pfgIs802_3 = TRUE;

		ucDSAP = *(PUINT_8) &aucLookAheadBuf[ETH_LLC_OFFSET];
		ucSSAP = *(PUINT_8) &aucLookAheadBuf[ETH_LLC_OFFSET + 1];
		ucControl = *(PUINT_8) &aucLookAheadBuf[ETH_LLC_OFFSET + 2];

		aucOUI[0] = *(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET];
		aucOUI[1] = *(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET + 1];
		aucOUI[2] = *(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET + 2];

		if (ucDSAP == ETH_LLC_DSAP_SNAP &&
		    ucSSAP == ETH_LLC_SSAP_SNAP &&
		    ucControl == ETH_LLC_CONTROL_UNNUMBERED_INFORMATION &&
		    aucOUI[0] == ETH_SNAP_BT_SIG_OUI_0 &&
		    aucOUI[1] == ETH_SNAP_BT_SIG_OUI_1 && aucOUI[2] == ETH_SNAP_BT_SIG_OUI_2) {
			*pfgIsPAL = TRUE;

			/* check if Security Frame */
			if ((*(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET + 3]) ==
			    ((BOW_PROTOCOL_ID_SECURITY_FRAME & 0xFF00) >> 8)
			    && (*(PUINT_8) &aucLookAheadBuf[ETH_SNAP_OFFSET + 4]) ==
			    (BOW_PROTOCOL_ID_SECURITY_FRAME & 0xFF)) {
				*pfgIs1X = TRUE;
			}
		}
	}
	/* 4 <4> Return the value of Priority Parameter. */
	*pucPriorityParam = ucUserPriority;

	/* 4 <5> Retrieve Packet Information - DA */
	/* Packet Length/ Destination Address */
	*pu4PacketLen = u4PacketLen;
	kalMemCopy(pucEthDestAddr, aucLookAheadBuf, PARAM_MAC_ADDR_LEN);

	return TRUE;

}				/* end of kalQoSFrameClassifier() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is the callback function of master timer
*
* \param[in] systemSpecific1
* \param[in] miniportAdapterContext Pointer to GLUE Data Structure
* \param[in] systemSpecific2
* \param[in] systemSpecific3
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalTimerEvent(IN PVOID systemSpecific1,
	      IN PVOID miniportAdapterContext, IN PVOID systemSpecific2, IN PVOID systemSpecific3)
{
	P_GLUE_INFO_T prGlueInfo = (P_GLUE_INFO_T) miniportAdapterContext;
	PFN_TIMER_CALLBACK pfTimerFunc = (PFN_TIMER_CALLBACK) prGlueInfo->pvTimerFunc;

	GLUE_SPIN_LOCK_DECLARATION();

	pfTimerFunc(prGlueInfo);
}

/*----------------------------------------------------------------------------*/
/*!
* \brief Timer Initialization Procedure
*
* \param[in] prGlueInfo     Pointer to GLUE Data Structure
* \param[in] prTimerHandler Pointer to timer handling function, whose only
*                           argument is "prAdapter"
*
* \retval none
*
*/
/*----------------------------------------------------------------------------*/
VOID kalOsTimerInitialize(IN P_GLUE_INFO_T prGlueInfo, IN PVOID prTimerHandler)
{
	prGlueInfo->pvTimerFunc = prTimerHandler;

	/* Setup master timer. This master timer is the root timer for following
	 * management timers.
	 * Note: NdisMInitializeTimer() only could be called after
	 * NdisMSetAttributesEx() */
	NdisMInitializeTimer(&prGlueInfo->rMasterTimer,
			     prGlueInfo->rMiniportAdapterHandle,
			     (PNDIS_TIMER_FUNCTION) kalTimerEvent, (PVOID) prGlueInfo);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to set the time to do the time out check.
*
* \param[in] prGlueInfo Pointer to GLUE Data Structure
* \param[in] rInterval  Time out interval from current time.
*
* \retval TRUE Success.
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalSetTimer(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Interval)
{
	ASSERT(prGlueInfo);

	NdisMSetTimer(&prGlueInfo->rMasterTimer, u4Interval);
	return TRUE;		/* success */
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is called to cancel
*
* \param[in] prGlueInfo Pointer to GLUE Data Structure
*
* \retval TRUE  :   Timer has been canceled
*         FALAE :   Timer doens't exist
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalCancelTimer(IN P_GLUE_INFO_T prGlueInfo)
{
	BOOLEAN fgTimerCancelled;

	ASSERT(prGlueInfo);

	GLUE_CLEAR_FLAG(prGlueInfo, GLUE_FLAG_TIMEOUT);

	NdisMCancelTimer(&prGlueInfo->rMasterTimer, &fgTimerCancelled);

	return fgTimerCancelled;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief command timeout call-back function
 *
 * \param[in] prGlueInfo Pointer to the GLUE data structure.
 *
 * \retval (none)
 */
/*----------------------------------------------------------------------------*/
VOID kalTimeoutHandler(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	GLUE_SET_FLAG(prGlueInfo, GLUE_FLAG_TIMEOUT);

	/* Notify TxServiceThread for timeout event */
	GLUE_SET_EVENT(prGlueInfo);

	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to clear pending OID queued in pvOidEntry
*        (after windowsSetInformation() and before gl_kal_common.c processing)
*
* \param pvGlueInfo Pointer of GLUE Data Structure

* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalOidClearance(IN P_GLUE_INFO_T prGlueInfo)
{
	P_WLAN_REQ_ENTRY pvEntry;

	ASSERT(prGlueInfo);

	if (_InterlockedAnd(&prGlueInfo->rHifInfo.u4ReqFlag, ~REQ_FLAG_OID) & REQ_FLAG_OID) {
		pvEntry = (P_WLAN_REQ_ENTRY) prGlueInfo->pvOidEntry;

		ASSERT(pvEntry);

		GLUE_DEC_REF_CNT(prGlueInfo->i4OidPendingCount);
		ASSERT(prGlueInfo->i4OidPendingCount == 0);

		if (prGlueInfo->fgSetOid) {
			NdisMSetInformationComplete(prGlueInfo->rMiniportAdapterHandle,
						    NDIS_STATUS_FAILURE);
		} else {
			NdisMQueryInformationComplete(prGlueInfo->rMiniportAdapterHandle,
						      NDIS_STATUS_FAILURE);
		}
	}
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to retrieve overriden netweork address
*
* \param pvGlueInfo Pointer of GLUE Data Structure

* \retval TRUE
*         FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalRetrieveNetworkAddress(IN P_GLUE_INFO_T prGlueInfo, IN OUT PARAM_MAC_ADDRESS * prMacAddr)
{
	ASSERT(prGlueInfo);

	if (prGlueInfo->rRegInfo.aucMacAddr[0] & BIT(0)) {	/* invalid MAC address */
		return FALSE;
	} else {
		COPY_MAC_ADDR(prMacAddr, prGlueInfo->rRegInfo.aucMacAddr);

		return TRUE;
	}
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to update netweork address to glue layer
*
* \param pvGlueInfo Pointer of GLUE Data Structure
* \param pucMacAddr Pointer to 6-bytes MAC address
*
* \retval TRUE
*         FALSE
*/
/*----------------------------------------------------------------------------*/
VOID kalUpdateMACAddress(IN P_GLUE_INFO_T prGlueInfo, IN PUINT_8 pucMacAddr)
{
	ASSERT(prGlueInfo);
	ASSERT(pucMacAddr);

	/* windows doesn't need to support this routine */
	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to inform glue layer for address update
*
* \param prGlueInfo Pointer of GLUE Data Structure
* \param rMacAddr   Reference to MAC address
*
* \retval TRUE
*         FALSE
*/
/*----------------------------------------------------------------------------*/
VOID kalUpdateNetworkAddress(IN P_GLUE_INFO_T prGlueInfo, IN PARAM_MAC_ADDRESS rMacAddr)
{
	ASSERT(prGlueInfo);

	/* windows don't need to handle this */
	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to load firmware image
*
* \param pvGlueInfo     Pointer of GLUE Data Structure
* \param ppvMapFileBuf  Pointer of pointer to memory-mapped firmware image
* \param pu4FileLength  File length and memory mapped length as well

* \retval Map File Handle, used for unammping
*/
/*----------------------------------------------------------------------------*/
PVOID
kalFirmwareImageMapping(IN P_GLUE_INFO_T prGlueInfo,
			OUT PPVOID ppvMapFileBuf, OUT PUINT_32 pu4FileLength)
{
	NDIS_HANDLE *prFileHandleFwImg;
	NDIS_STRING rFileWifiRam;

	DEBUGFUNC("kalFirmwareImageMapping");

	ASSERT(prGlueInfo);
	ASSERT(ppvMapFileBuf);
	ASSERT(pu4FileLength);

	if ((prFileHandleFwImg = kalMemAlloc(sizeof(NDIS_HANDLE), VIR_MEM_TYPE)) == NULL) {
		DBGLOG(INIT, ERROR, ("Fail to allocate memory for NDIS_HANDLE!\n"));
		return NULL;
	}

	/* Mapping FW image from file */
#if defined(MT6620) && CFG_MULTI_ECOVER_SUPPORT
	if (wlanGetEcoVersion(prGlueInfo->prAdapter) >= 6) {
		NdisInitUnicodeString(&rFileWifiRam, prGlueInfo->rRegInfo.aucFwImgFilenameE6);
	} else {
		NdisInitUnicodeString(&rFileWifiRam, prGlueInfo->rRegInfo.aucFwImgFilename);
	}
#else
	NdisInitUnicodeString(&rFileWifiRam, prGlueInfo->rRegInfo.aucFwImgFilename);
#endif
	if (!imageFileMapping(rFileWifiRam, prFileHandleFwImg, ppvMapFileBuf, pu4FileLength)) {
		DBGLOG(INIT, ERROR, ("Fail to load FW image from file!\n"));
		kalMemFree(prFileHandleFwImg, VIR_MEM_TYPE, sizeof(NDIS_HANDLE));
		return NULL;
	}

	return (PVOID) prFileHandleFwImg;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to unload firmware image mapped memory
*
* \param pvGlueInfo     Pointer of GLUE Data Structure
* \param pvFwHandle     Pointer to mapping handle
* \param pvMapFileBuf   Pointer to memory-mapped firmware image
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalFirmwareImageUnmapping(IN P_GLUE_INFO_T prGlueInfo, IN PVOID prFwHandle, IN PVOID pvMapFileBuf)
{
	NDIS_HANDLE rFileHandleFwImg;

	DEBUGFUNC("kalFirmwareImageUnmapping");

	ASSERT(prGlueInfo);
	ASSERT(prFwHandle);
	ASSERT(pvMapFileBuf);

	rFileHandleFwImg = *(NDIS_HANDLE *) prFwHandle;

	/* unmap */
	imageFileUnMapping(rFileHandleFwImg, pvMapFileBuf);

	/* free handle */
	kalMemFree(prFwHandle, VIR_MEM_TYPE, sizeof(NDIS_HANDLE));

	return;

}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to check if card is removed
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval TRUE:     card is removed
*         FALSE:    card is still attached
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalIsCardRemoved(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	return prGlueInfo->fgIsCardRemoved;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to flush pending TX packets in glue layer
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval
*/
/*----------------------------------------------------------------------------*/
VOID kalFlushPendingTxPackets(IN P_GLUE_INFO_T prGlueInfo)
{
	P_QUE_T prTxQue;
	P_QUE_ENTRY_T prQueueEntry;
	PVOID prPacket;

	ASSERT(prGlueInfo);

	prTxQue = &(prGlueInfo->rTxQueue);

	if (prGlueInfo->i4TxPendingFrameNum) {
		while (TRUE) {
			GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);
			QUEUE_REMOVE_HEAD(prTxQue, prQueueEntry, P_QUE_ENTRY_T);
			GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_TX_QUE);

			if (prQueueEntry == NULL) {
				break;
			}

			prPacket = GLUE_GET_PKT_DESCRIPTOR(prQueueEntry);

			kalSendComplete(prGlueInfo, prPacket, WLAN_STATUS_SUCCESS);
		}
	}
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to retrieve the number of pending TX packets
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalGetTxPendingFrameCount(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	return (UINT_32) (prGlueInfo->i4TxPendingFrameNum);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to retrieve the number of pending commands
*        (including MMPDU, 802.1X and command packets)
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalGetTxPendingCmdCount(IN P_GLUE_INFO_T prGlueInfo)
{
	P_QUE_T prCmdQue;

	ASSERT(prGlueInfo);
	prCmdQue = &prGlueInfo->rCmdQueue;

	return prCmdQue->u4NumElem;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is get indicated media state
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
ENUM_PARAM_MEDIA_STATE_T kalGetMediaStateIndicated(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	return prGlueInfo->eParamMediaStateIndicated;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to set indicated media state
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalSetMediaStateIndicated(IN P_GLUE_INFO_T prGlueInfo,
			  IN ENUM_PARAM_MEDIA_STATE_T eParamMediaStateIndicate)
{
	ASSERT(prGlueInfo);

	prGlueInfo->eParamMediaStateIndicated = eParamMediaStateIndicate;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to get firmware load address from registry
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalGetFwLoadAddress(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	return prGlueInfo->rRegInfo.u4LoadAddress;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to get firmware start address from registry
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalGetFwStartAddress(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	return prGlueInfo->rRegInfo.u4StartAddress;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to clear pending OID staying in command queue
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalOidCmdClearance(IN P_GLUE_INFO_T prGlueInfo)
{
	P_QUE_T prCmdQue;
	QUE_T rTempCmdQue;
	P_QUE_T prTempCmdQue = &rTempCmdQue;
	P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T) NULL;
	P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T) NULL;

	ASSERT(prGlueInfo);

	/* Clear pending OID in prGlueInfo->rCmdQueue */
	prCmdQue = &prGlueInfo->rCmdQueue;

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);

	QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	while (prQueueEntry) {

		if (((P_CMD_INFO_T) prQueueEntry)->fgIsOid) {
			prCmdInfo = (P_CMD_INFO_T) prQueueEntry;
			break;
		} else {
			QUEUE_INSERT_TAIL(prCmdQue, prQueueEntry);
		}

		QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	}

	QUEUE_CONCATENATE_QUEUES(prCmdQue, prTempCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);

	if (prCmdInfo) {
		if (prCmdInfo->pfCmdTimeoutHandler) {
			prCmdInfo->pfCmdTimeoutHandler(prGlueInfo->prAdapter, prCmdInfo);
		} else {
			kalOidComplete(prGlueInfo, prCmdInfo->fgSetQuery, 0, WLAN_STATUS_FAILURE);
		}

		cmdBufFreeCmdInfo(prGlueInfo->prAdapter, prCmdInfo);
	}
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to insert command into prCmdQueue
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*        prQueueEntry   Pointer of queue entry to be inserted
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalEnqueueCommand(IN P_GLUE_INFO_T prGlueInfo, IN P_QUE_ENTRY_T prQueueEntry)
{
	P_QUE_T prCmdQue;

	ASSERT(prGlueInfo);
	ASSERT(prQueueEntry);

	prCmdQue = &prGlueInfo->rCmdQueue;

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_INSERT_TAIL(prCmdQue, prQueueEntry);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to clear all pending security frames
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalClearSecurityFrames(IN P_GLUE_INFO_T prGlueInfo)
{
	P_QUE_T prCmdQue;
	QUE_T rTempCmdQue, rReturnCmdQue;
	P_QUE_T prTempCmdQue = &rTempCmdQue, prReturnCmdQue = &rReturnCmdQue;
	P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T) NULL;

	P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T) NULL;

	ASSERT(prGlueInfo);

	QUEUE_INITIALIZE(prReturnCmdQue);
	/* Clear pending security frames in prGlueInfo->rCmdQueue */
	prCmdQue = &prGlueInfo->rCmdQueue;

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);

	QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	while (prQueueEntry) {
		prCmdInfo = (P_CMD_INFO_T) prQueueEntry;

		if (prCmdInfo->eCmdType == COMMAND_TYPE_SECURITY_FRAME) {
			if (prCmdInfo->pfCmdTimeoutHandler) {
				prCmdInfo->pfCmdTimeoutHandler(prGlueInfo->prAdapter, prCmdInfo);
			} else {
				wlanReleaseCommand(prGlueInfo->prAdapter, prCmdInfo,
						   TX_RESULT_QUEUE_CLEARANCE);
			}
			cmdBufFreeCmdInfo(prGlueInfo->prAdapter, prCmdInfo);
		} else {
			QUEUE_INSERT_TAIL(prReturnCmdQue, prQueueEntry);
		}

		QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	}

	/* insert return queue back to cmd queue */
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_CONCATENATE_QUEUES_HEAD(prCmdQue, prReturnCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to clear pending security frames
*        belongs to dedicated network type
*
* \param prGlueInfo         Pointer of GLUE Data Structure
* \param eNetworkTypeIdx    Network Type Index
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalClearSecurityFramesByBssIdx(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucBssIndex)
{
	P_QUE_T prCmdQue;
	QUE_T rTempCmdQue, rReturnCmdQue;
	P_QUE_T prTempCmdQue = &rTempCmdQue, prReturnCmdQue = &rReturnCmdQue;
	P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T) NULL;

	P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T) NULL;

	ASSERT(prGlueInfo);

	QUEUE_INITIALIZE(prReturnCmdQue);
	/* Clear pending security frames in prGlueInfo->rCmdQueue */
	prCmdQue = &prGlueInfo->rCmdQueue;

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);

	QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	while (prQueueEntry) {
		prCmdInfo = (P_CMD_INFO_T) prQueueEntry;

		if (prCmdInfo->eCmdType == COMMAND_TYPE_SECURITY_FRAME &&
		    prCmdInfo->ucBssIndex == ucBssIndex) {
			if (prCmdInfo->pfCmdTimeoutHandler) {
				prCmdInfo->pfCmdTimeoutHandler(prGlueInfo->prAdapter, prCmdInfo);
			} else {
				wlanReleaseCommand(prGlueInfo->prAdapter, prCmdInfo,
						   TX_RESULT_QUEUE_CLEARANCE);
			}
			cmdBufFreeCmdInfo(prGlueInfo->prAdapter, prCmdInfo);
		} else {
			QUEUE_INSERT_TAIL(prReturnCmdQue, prQueueEntry);
		}

		QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	}

	/* insert return queue back to cmd queue */
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_CONCATENATE_QUEUES_HEAD(prCmdQue, prReturnCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to clear all pending management frames
*
* \param prGlueInfo     Pointer of GLUE Data Structure
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalClearMgmtFrames(IN P_GLUE_INFO_T prGlueInfo)
{
	P_QUE_T prCmdQue;
	QUE_T rTempCmdQue, rReturnCmdQue;
	P_QUE_T prTempCmdQue = &rTempCmdQue, prReturnCmdQue = &rReturnCmdQue;
	P_QUE_ENTRY_T prQueueEntry = (P_QUE_ENTRY_T) NULL;

	P_CMD_INFO_T prCmdInfo = (P_CMD_INFO_T) NULL;

	ASSERT(prGlueInfo);

	QUEUE_INITIALIZE(prReturnCmdQue);
	/* Clear pending management frames in prGlueInfo->rCmdQueue */
	prCmdQue = &prGlueInfo->rCmdQueue;

	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_MOVE_ALL(prTempCmdQue, prCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);

	QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	while (prQueueEntry) {
		prCmdInfo = (P_CMD_INFO_T) prQueueEntry;

		if (prCmdInfo->eCmdType == COMMAND_TYPE_MANAGEMENT_FRAME) {
			wlanReleaseCommand(prGlueInfo->prAdapter, prCmdInfo,
					   TX_RESULT_QUEUE_CLEARANCE);
			cmdBufFreeCmdInfo(prGlueInfo->prAdapter, prCmdInfo);
		} else {
			QUEUE_INSERT_TAIL(prReturnCmdQue, prQueueEntry);
		}

		QUEUE_REMOVE_HEAD(prTempCmdQue, prQueueEntry, P_QUE_ENTRY_T);
	}

	/* insert return queue back to cmd queue */
	GLUE_ACQUIRE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
	QUEUE_CONCATENATE_QUEUES_HEAD(prCmdQue, prReturnCmdQue);
	GLUE_RELEASE_SPIN_LOCK(prGlueInfo, SPIN_LOCK_CMD_QUE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is used to generate a random number
*
* \param none
*
* \retval UINT_32
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalRandomNumber(VOID)
{
	UINT_32 u4Seed;

	u4Seed = kalGetTimeTick();

	return RtlRandomEx(&u4Seed);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This routine is not used by NDIS
*
* \param prGlueInfo     Pointer of GLUE_INFO_T
*        eNetTypeIdx    Index of Network Type
*        rStatus        Status
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalScanDone(IN P_GLUE_INFO_T prGlueInfo,
	    IN ENUM_KAL_NETWORK_TYPE_INDEX_T eNetTypeIdx, IN WLAN_STATUS rStatus)
{
	ASSERT(prGlueInfo);

	/* check for system configuration for generating error message on scan list */
	wlanCheckSystemConfiguration(prGlueInfo->prAdapter);

	return;
}


#if CFG_ENABLE_BT_OVER_WIFI
/*----------------------------------------------------------------------------*/
/*!
* \brief to indicate event for Bluetooth over Wi-Fi
*
* \param[in]
*           prGlueInfo
*           prEvent
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID kalIndicateBOWEvent(IN P_GLUE_INFO_T prGlueInfo, IN P_AMPC_EVENT prEvent)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi state from glue layer
*
* \param[in]
*           prGlueInfo
* \return
*           ENUM_BOW_DEVICE_STATE
*/
/*----------------------------------------------------------------------------*/
ENUM_BOW_DEVICE_STATE kalGetBowState(IN P_GLUE_INFO_T prGlueInfo, IN PARAM_MAC_ADDRESS rPeerAddr)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return BOW_DEVICE_STATE_DISCONNECTED;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Bluetooth-over-Wi-Fi state in glue layer
*
* \param[in]
*           prGlueInfo
*           eBowState
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalSetBowState(IN P_GLUE_INFO_T prGlueInfo,
	       IN ENUM_BOW_DEVICE_STATE eBowState, IN PARAM_MAC_ADDRESS rPeerAddr)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi global state
*
* \param[in]
*           prGlueInfo
*
* \return
*           BOW_DEVICE_STATE_DISCONNECTED
*               in case there is no BoW connection or
*               BoW connection under initialization
*
*           BOW_DEVICE_STATE_STARTING
*               in case there is no BoW connection but
*               some BoW connection under initialization
*
*           BOW_DEVICE_STATE_CONNECTED
*               in case there is any BoW connection available
*/
/*----------------------------------------------------------------------------*/
ENUM_BOW_DEVICE_STATE kalGetBowGlobalState(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return BOW_DEVICE_STATE_DISCONNECTED;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi operating frequency
*
* \param[in]
*           prGlueInfo
*
* \return
*           in unit of KHz
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalGetBowFreqInKHz(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi role
*
* \param[in]
*           prGlueInfo
*
* \return
*           0: Responder
*           1: Initiator
*/
/*----------------------------------------------------------------------------*/
UINT_8 kalGetBowRole(IN P_GLUE_INFO_T prGlueInfo, IN PARAM_MAC_ADDRESS rPeerAddr)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Bluetooth-over-Wi-Fi role
*
* \param[in]
*           prGlueInfo
*           ucRole
*                   0: Responder
*                   1: Initiator
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID kalSetBowRole(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucRole, IN PARAM_MAC_ADDRESS rPeerAddr)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get available Bluetooth-over-Wi-Fi physical link number
*
* \param[in]
*           prGlueInfo
* \return
*           UINT_32
*               how many physical links are aviailable
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalGetBowAvailablePhysicalLinkCount(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* BT 3.0 + HS not implemented */

	return 0;
}
#endif

#if CFG_ENABLE_WIFI_DIRECT
/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Wi-Fi Direct state from glue layer
*
* \param[in]
*           prGlueInfo
*           rPeerAddr
* \return
*           ENUM_BOW_DEVICE_STATE
*/
/*----------------------------------------------------------------------------*/
ENUM_PARAM_MEDIA_STATE_T kalP2PGetState(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */

	return PARAM_MEDIA_STATE_DISCONNECTED;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Wi-Fi Direct state in glue layer
*
* \param[in]
*           prGlueInfo
*           eBowState
*           rPeerAddr
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PSetState(IN P_GLUE_INFO_T prGlueInfo,
	       IN ENUM_PARAM_MEDIA_STATE_T eState, IN PARAM_MAC_ADDRESS rPeerAddr, IN UINT_8 ucRole)
{
	ASSERT(prGlueInfo);

	switch (eState) {
	case PARAM_MEDIA_STATE_CONNECTED:
		/* TODO: indicate IWEVP2PSTACNT */
		break;

	case PARAM_MEDIA_STATE_DISCONNECTED:
		/* TODO: indicate IWEVP2PSTADISCNT */
		break;

	default:
		ASSERT(0);
		break;
	}
	/* Wi-Fi Direct not implemented yet */

	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Wi-Fi Direct operating frequency
*
* \param[in]
*           prGlueInfo
*
* \return
*           in unit of KHz
*/
/*----------------------------------------------------------------------------*/
UINT_32 kalP2PGetFreqInKHz(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */

	return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Bluetooth-over-Wi-Fi role
*
* \param[in]
*           prGlueInfo
*
* \return
*           0: P2P Device
*           1: Group Client
*           2: Group Owner
*/
/*----------------------------------------------------------------------------*/
UINT_8 kalP2PGetRole(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */

	return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to set Wi-Fi Direct role
*
* \param[in]
*           prGlueInfo
*           ucResult
*                   0: successful
*                   1: error
*           ucRole
*                   0: P2P Device
*                   1: Group Client
*                   2: Group Owner
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PSetRole(IN P_GLUE_INFO_T prGlueInfo,
	      IN UINT_8 ucResult, IN PUINT_8 pucSSID, IN UINT_8 ucSSIDLen, IN UINT_8 ucRole)
{
	ASSERT(prGlueInfo);
	ASSERT(ucRole <= 2);

	/* Wi-Fi Direct not implemented yet */

	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief indicate an event to supplicant for device found
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval TRUE  Success.
* \retval FALSE Failure
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalP2PIndicateFound(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	/* TODO: indicate IWEVP2PDVCFND event */

	return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief indicate an event to supplicant for device connection request
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID kalP2PIndicateConnReq(IN P_GLUE_INFO_T prGlueInfo, IN PUINT_8 pucDevName, IN INT_32 u4NameLength, IN PARAM_MAC_ADDRESS rPeerAddr, IN UINT_8 ucDevType,	/* 0: P2P Device / 1: GC / 2: GO */
			   IN INT_32 i4ConfigMethod, IN INT_32 i4ActiveConfigMethod)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	/* TODO: indicate IWEVP2PDVCRQ event */

	return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief indicate an event to supplicant for device invitation request
*
* \param[in] prGlueInfo Pointer of GLUE_INFO_T
*
* \retval none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PInvitationIndication(IN P_GLUE_INFO_T prGlueInfo,
			   IN P_P2P_DEVICE_DESC_T prP2pDevDesc,
			   IN PUINT_8 pucSsid,
			   IN UINT_8 ucSsidLen,
			   IN UINT_8 ucOperatingChnl, IN UINT_8 ucInvitationType)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	/* TODO: indicate IWEVP2PDVCRQ event */

	return;
}



/*----------------------------------------------------------------------------*/
/*!
* \brief to set the cipher for p2p
*
* \param[in]
*           prGlueInfo
*           u4Cipher
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID kalP2PSetCipher(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Cipher)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the cipher, return for cipher is ccmp
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE: cipher is ccmp
*           FALSE: cipher is none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalP2PGetCipher(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the cipher, return for cipher is ccmp
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE: cipher is ccmp
*           FALSE: cipher is none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalP2PGetTkipCipher(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the cipher, return for cipher is ccmp
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE: cipher is ccmp
*           FALSE: cipher is none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalP2PGetCcmpCipher(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
	return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to get the wsc ie length
*
* \param[in]
*           prGlueInfo
*
* \return
*           The WPS IE length
*/
/*----------------------------------------------------------------------------*/
UINT_16 kalP2PCalWSC_IELen(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucType)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */

	return 0;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to copy the wsc ie setting from p2p supplicant
*
* \param[in]
*           prGlueInfo
*
* \return
*           The WPS IE length
*/
/*----------------------------------------------------------------------------*/
VOID kalP2PGenWSC_IE(IN P_GLUE_INFO_T prGlueInfo, IN UINT_8 ucType, IN PUINT_8 pucBuffer)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to update the assoc req to p2p
*
* \param[in]
*           prGlueInfo
*           eBowState
*           rPeerAddr
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID
kalP2PUpdateAssocInfo(IN P_GLUE_INFO_T prGlueInfo,
		      IN PUINT_8 pucFrameBody,
		      IN UINT_32 u4FrameBodyLen, IN BOOLEAN fgReassocRequest)
{
	ASSERT(prGlueInfo);

	/* Wi-Fi Direct not implemented yet */
}
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief to check if configuration file (NVRAM/Registry) exists
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalIsConfigurationExist(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	/* for windows platform, always return TRUE */
	return TRUE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve Registry information
*
* \param[in]
*           prGlueInfo
*
* \return
*           Pointer of REG_INFO_T
*/
/*----------------------------------------------------------------------------*/
P_REG_INFO_T kalGetConfiguration(IN P_GLUE_INFO_T prGlueInfo)
{
	ASSERT(prGlueInfo);

	return &(prGlueInfo->rRegInfo);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief to retrieve version information of corresponding configuration file
*
* \param[in]
*           prGlueInfo
*
* \param[out]
*           pu2Part1CfgOwnVersion
*           pu2Part1CfgPeerVersion
*           pu2Part2CfgOwnVersion
*           pu2Part2CfgPeerVersion
*
* \return
*           NONE
*/
/*----------------------------------------------------------------------------*/
VOID
kalGetConfigurationVersion(IN P_GLUE_INFO_T prGlueInfo,
			   OUT PUINT_16 pu2Part1CfgOwnVersion,
			   OUT PUINT_16 pu2Part1CfgPeerVersion,
			   OUT PUINT_16 pu2Part2CfgOwnVersion, OUT PUINT_16 pu2Part2CfgPeerVersion)
{
	ASSERT(prGlueInfo);

	ASSERT(pu2Part1CfgOwnVersion);
	ASSERT(pu2Part1CfgPeerVersion);
	ASSERT(pu2Part2CfgOwnVersion);
	ASSERT(pu2Part2CfgPeerVersion);

	/* Windows uses registry instead, */
	/* and we'll always have a default value to use if */
	/* the registry entry doesn't exist, so we use UINT16_MAX / 0 pair */
	/* as version information to keep maximum compatibility */

	*pu2Part1CfgOwnVersion = UINT16_MAX;
	*pu2Part1CfgPeerVersion = 0;

	*pu2Part2CfgOwnVersion = UINT16_MAX;
	*pu2Part2CfgPeerVersion = 0;

	return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief API for reading data on NVRAM
*
* \param[in]
*           prGlueInfo
*           u4Offset
* \param[out]
*           pu2Data
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalCfgDataRead16(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Offset, OUT PUINT_16 pu2Data)
{
	ASSERT(prGlueInfo);

	/* windows family doesn't support NVRAM access */
	return FALSE;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief API for writing data on NVRAM
*
* \param[in]
*           prGlueInfo
*           u4Offset
*           u2Data
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalCfgDataWrite16(IN P_GLUE_INFO_T prGlueInfo, IN UINT_32 u4Offset, IN UINT_16 u2Data)
{
	ASSERT(prGlueInfo);

	/* windows family doesn't support NVRAM access */
	return FALSE;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief to check if the WPS is active or not
*
* \param[in]
*           prGlueInfo
*
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalWSCGetActiveState(IN P_GLUE_INFO_T prGlueInfo)
{
	/* Windows family does not support WSC. */
	return (FALSE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief update RSSI and LinkQuality to GLUE layer
*
* \param[in]
*           prGlueInfo
*           eNetTypeIdx
*           cRssi
*           cLinkQuality
*
* \return
*           None
*/
/*----------------------------------------------------------------------------*/
VOID
kalUpdateRSSI(IN P_GLUE_INFO_T prGlueInfo,
	      IN ENUM_KAL_NETWORK_TYPE_INDEX_T eNetTypeIdx, IN INT_8 cRssi, IN INT_8 cLinkQuality)
{
	ASSERT(prGlueInfo);

	switch (eNetTypeIdx) {
	case KAL_NETWORK_TYPE_AIS_INDEX:
		break;

	default:
		break;

	}

	return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Dispatch pre-allocated I/O buffer
*
* \param[in]
*           u4AllocSize
*
* \return
*           PVOID for pointer of pre-allocated I/O buffer
*/
/*----------------------------------------------------------------------------*/
PVOID kalAllocateIOBuffer(IN UINT_32 u4AllocSize)
{
	return kalMemAlloc(u4AllocSize, PHY_MEM_TYPE);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief Release all dispatched I/O buffer
*
* \param[in]
*           pvAddr
*           u4Size
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID kalReleaseIOBuffer(IN PVOID pvAddr, IN UINT_32 u4Size)
{
	kalMemFree(pvAddr, PHY_MEM_TYPE, u4Size);
}


#if CFG_CHIP_RESET_SUPPORT
/*----------------------------------------------------------------------------*/
/*!
* \brief Whole-chip Reset Trigger
*
* \param[in]
*           none
*
* \return
*           none
*/
/*----------------------------------------------------------------------------*/
VOID glSendResetRequest(VOID)
{
	/* no implementation for Win32 platform */
	return;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is called for checking if MT6620 is resetting
 *
 * @param   None
 *
 * @retval  TRUE
 *          FALSE
 */
/*----------------------------------------------------------------------------*/
BOOLEAN kalIsResetting(VOID)
{
	return FALSE;
}
#endif


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is called for writing data to file
 *
 * @param [in]
 *              pucPath
 *              fgDoAppend
 *              pucData
 *              u4Size
 *
 * @retval  length
 *
 */
/*----------------------------------------------------------------------------*/
UINT_32 kalWriteToFile(const PUINT_8 pucPath, BOOLEAN fgDoAppend, PUINT_8 pucData, UINT_32 u4Size)
{
	/* no implementation for Win32 platform */
	HANDLE hFile;
	IO_STATUS_BLOCK ioStatusBlock;
	HANDLE handle;
	NTSTATUS ntstatus;
	LARGE_INTEGER byteOffset;
	UNICODE_STRING uniName;
	OBJECT_ATTRIBUTES objAttr;
	/* 1.Path information */
	RtlInitUnicodeString(&uniName, pucPath);

	InitializeObjectAttributes(&objAttr, &uniName,
				   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
	/*  */

	if (KeGetCurrentIrql() != PASSIVE_LEVEL)
		return STATUS_INVALID_DEVICE_STATE;
	if (!fgDoAppend) {
		ntstatus = ZwCreateFile(&handle,
					GENERIC_WRITE,
					&objAttr, &ioStatusBlock, NULL,
					FILE_ATTRIBUTE_NORMAL,
					0,
					FILE_OVERWRITE_IF, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);

	} else {
		ntstatus = ZwOpenFile(&handle,
				      FILE_APPEND_DATA,
				      &objAttr, &ioStatusBlock,
				      FILE_SHARE_READ | FILE_SHARE_WRITE,
				      FILE_SYNCHRONOUS_IO_NONALERT);


	}

	ntstatus = ZwWriteFile(handle, NULL, NULL, NULL, &ioStatusBlock,
			       pucData, u4Size, NULL, NULL);
	ZwClose(handle);
	return 0;
}


/*----------------------------------------------------------------------------*/
/*!
 * @brief This routine is co
 *
 * @param [in]
 *
 * @retval  length
 *
 */
/*----------------------------------------------------------------------------*/
UINT_32
kal_sprintf_ddk(const PUINT_8 pucPath,
		UINT_32 u4size, const char *fmt1, const char *fmt2, const char *fmt3)
{
#if 0
	RtlStringCbPrintfW(pucPath, u4size,
			   L"\\SystemRoot\\dump_%ld_0x%08lX_%ld.hex", fmt1, fmt2, fmt3);
#else
	RtlStringCbPrintfW(pucPath, u4size, L"\\SystemRoot\\dump.hex");

#endif
	return 0;
}

UINT_32 kal_sprintf_done_ddk(const PUINT_8 pucPath, UINT_32 u4size)
{
	RtlStringCbPrintfW(pucPath, u4size, L"\\SystemRoot\\dump_done.txt");

	return 0;
}

#if CFG_SUPPORT_SDIO_READ_WRITE_PATTERN
/*----------------------------------------------------------------------------*/
/*!
* \brief    To configure SDIO test pattern mode
*
* \param[in]
*           prGlueInfo
*           fgEn
*           fgRead
*
* \return
*           TRUE
*           FALSE
*/
/*----------------------------------------------------------------------------*/
BOOLEAN kalSetSdioTestPattern(IN P_GLUE_INFO_T prGlueInfo, IN BOOLEAN fgEn, IN BOOLEAN fgRead)
{
	const UINT_8 aucPattern[] = {
		0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55,
		0xaa, 0x55, 0x80, 0x80, 0x80, 0x7f, 0x80, 0x80,
		0x80, 0x7f, 0x7f, 0x7f, 0x80, 0x7f, 0x7f, 0x7f,
		0x40, 0x40, 0x40, 0xbf, 0x40, 0x40, 0x40, 0xbf,
		0xbf, 0xbf, 0x40, 0xbf, 0xbf, 0xbf, 0x20, 0x20,
		0x20, 0xdf, 0x20, 0x20, 0x20, 0xdf, 0xdf, 0xdf,
		0x20, 0xdf, 0xdf, 0xdf, 0x10, 0x10, 0x10, 0xef,
		0x10, 0x10, 0x10, 0xef, 0xef, 0xef, 0x10, 0xef,
		0xef, 0xef, 0x08, 0x08, 0x08, 0xf7, 0x08, 0x08,
		0x08, 0xf7, 0xf7, 0xf7, 0x08, 0xf7, 0xf7, 0xf7,
		0x04, 0x04, 0x04, 0xfb, 0x04, 0x04, 0x04, 0xfb,
		0xfb, 0xfb, 0x04, 0xfb, 0xfb, 0xfb, 0x02, 0x02,
		0x02, 0xfd, 0x02, 0x02, 0x02, 0xfd, 0xfd, 0xfd,
		0x02, 0xfd, 0xfd, 0xfd, 0x01, 0x01, 0x01, 0xfe,
		0x01, 0x01, 0x01, 0xfe, 0xfe, 0xfe, 0x01, 0xfe,
		0xfe, 0xfe, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,
		0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff,
		0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
		0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
		0x00, 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff,
		0x00, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0xff,
		0x00, 0x00, 0x00, 0xff
	};
	UINT_32 i;

	ASSERT(prGlueInfo);

	/* access to MCR_WTMCR to engage PRBS mode */
	prGlueInfo->fgEnSdioTestPattern = fgEn;
	prGlueInfo->fgSdioReadWriteMode = fgRead;

	if (fgRead == FALSE) {
		/* fill buffer for data to be written */
		for (i = 0; i < sizeof(aucPattern); i++) {
			prGlueInfo->aucSdioTestBuffer[i] = aucPattern[i];
		}
	}

	return TRUE;
}
#endif


VOID
kalReadyOnChannel(IN P_GLUE_INFO_T prGlueInfo,
		  IN UINT_64 u8Cookie,
		  IN ENUM_BAND_T eBand,
		  IN ENUM_CHNL_EXT_T eSco, IN UINT_8 ucChannelNum, IN UINT_32 u4DurationMs)
{
	/* no implementation yet */
	return;
}


VOID
kalRemainOnChannelExpired(IN P_GLUE_INFO_T prGlueInfo,
			  IN UINT_64 u8Cookie,
			  IN ENUM_BAND_T eBand, IN ENUM_CHNL_EXT_T eSco, IN UINT_8 ucChannelNum)
{
	/* no implementation yet */
	return;
}


VOID
kalIndicateMgmtTxStatus(IN P_GLUE_INFO_T prGlueInfo,
			IN UINT_64 u8Cookie,
			IN BOOLEAN fgIsAck, IN PUINT_8 pucFrameBuf, IN UINT_32 u4FrameLen)
{
	/* no implementation yet */
	return;
}


VOID kalIndicateRxMgmtFrame(IN P_GLUE_INFO_T prGlueInfo, IN P_SW_RFB_T prSwRfb)
{
	/* no implementation yet */
	return;
}


VOID kalSchedScanResults(IN P_GLUE_INFO_T prGlueInfo)
{
	/* no implementation yet */
	return;
}


VOID kalSchedScanStopped(IN P_GLUE_INFO_T prGlueInfo)
{
	/* no implementation yet */
	return;
}


BOOLEAN
kalGetEthDestAddr(IN P_GLUE_INFO_T prGlueInfo,
		  IN P_NATIVE_PACKET prPacket, OUT PUINT_8 pucEthDestAddr)
{
	PNDIS_BUFFER prNdisBuffer;
	UINT_32 u4PacketLen;

	UINT_32 u4ByteCount = 0;
	PNDIS_PACKET prNdisPacket = (PNDIS_PACKET) prPacket;

	DEBUGFUNC("kalGetEthDestAddr");

	ASSERT(prGlueInfo);
	ASSERT(pucEthDestAddr);

	/* 4 <1> Find out all buffer information */
	NdisQueryPacket(prNdisPacket, NULL, NULL, &prNdisBuffer, &u4PacketLen);

	if (u4PacketLen < 6) {
		DBGLOG(INIT, WARN, ("Invalid Ether packet length: %d\n", u4PacketLen));
		return FALSE;
	}
	/* 4 <2> Copy partial frame to local LOOK AHEAD Buffer */
	while (prNdisBuffer && u4ByteCount < 6) {
		PVOID pvAddr;
		UINT_32 u4Len;
		PNDIS_BUFFER prNextNdisBuffer;

#ifdef NDIS51_MINIPORT
		NdisQueryBufferSafe(prNdisBuffer, &pvAddr, &u4Len, HighPagePriority);
#else
		NdisQueryBuffer(prNdisBuffer, &pvAddr, &u4Len);
#endif

		if (pvAddr == (PVOID) NULL) {
			ASSERT(0);
			return FALSE;
		}

		if ((u4ByteCount + u4Len) >= 6) {
			kalMemCopy((PUINT_8) ((UINT_32) pucEthDestAddr + u4ByteCount),
				   pvAddr, (6 - u4ByteCount));
			break;
		} else {
			kalMemCopy((PUINT_8) ((UINT_32) pucEthDestAddr + u4ByteCount),
				   pvAddr, u4Len);
		}
		u4ByteCount += u4Len;

		NdisGetNextBuffer(prNdisBuffer, &prNextNdisBuffer);

		prNdisBuffer = prNextNdisBuffer;
	}

	return TRUE;
}
