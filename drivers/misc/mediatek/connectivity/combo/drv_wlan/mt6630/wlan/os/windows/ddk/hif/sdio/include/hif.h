/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/windows/ddk/hif/sdio/include/hif.h#1 $
*/
/*! \file   hif.h"
    \brief  Sdio specific structure for GLUE layer on WinXP

    Sdio specific structure for GLUE layer on WinXP
*/





#ifndef _HIF_H
#define _HIF_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <initguid.h>
#include <ntddsd.h>
#include <ntddk.h>
#include <wdm.h>

#ifdef MT5921
#define SDNDIS_REG_PATH TEXT("\\Comm\\MT5921")
#endif
#ifdef MT5922
#define SDNDIS_REG_PATH TEXT("\\Comm\\MT5922")
#endif

#ifdef __cplusplus
extern "C" {
	/* NDIS ddk header */
#include <ndis.h>
}
#endif
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define MAX_ACTIVE_REG_PATH         256
#define NIC_INTERFACE_TYPE          NdisInterfaceInternal
#define NIC_ATTRIBUTE               (NDIS_ATTRIBUTE_DESERIALIZE | NDIS_ATTRIBUTE_SURPRISE_REMOVE_OK)
#define NIC_DMA_MAPPED              0
#define NIC_MINIPORT_INT_REG        0
#define REQ_FLAG_HALT               (0x01)
#define REQ_FLAG_INT                (0x02)
#define REQ_FLAG_OID                (0x04)
#define REQ_FLAG_TIMER              (0x08)
#define REQ_FLAG_RESET              (0x10)
#define BLOCK_TRANSFER_LEN          (512)
/* Please make sure the MCR you wrote will not take any effect.
 * MCR_MIBSDR (0x00C4) has confirm with DE.
 */
#define SDIO_X86_WORKAROUND_WRITE_MCR   0x00C4
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/ typedef struct _DEVICE_EXTENSION {
	/* The bus driver object */
	PDEVICE_OBJECT PhysicalDeviceObject;

	/* Functional Device Object */
	PDEVICE_OBJECT FunctionalDeviceObject;

	/* Device object we call when submitting SDbus cmd */
	PDEVICE_OBJECT NextLowerDriverObject;

	/*Bus interface */
	SDBUS_INTERFACE_STANDARD BusInterface;

	/* SDIO funciton number */
	UCHAR FunctionNumber;
	BOOLEAN busInited;

	BOOLEAN fgIsSdioBlockModeEnabled;

} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

/* Windows glue layer's private data structure, which is
 * attached to adapter_p structure
 */
typedef struct _GL_HIF_INFO_T {
	/* using in SD open bus interface,  dbl add */
	DEVICE_EXTENSION dx;

	HANDLE rOidThreadHandle;
	PKTHREAD prOidThread;
	KEVENT rOidReqEvent;

	UINT_32 u4ReqFlag;	/* REQ_FLAG_XXX */

	UINT_32 u4BusClock;
	UINT_32 u4BusWidth;
	UINT_32 u4BlockLength;

	/* HW related */
	BOOLEAN fgIntReadClear;
	BOOLEAN fgMbxReadClear;

} GL_HIF_INFO_T, *P_GL_HIF_INFO_T;


/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif				/* _HIF_H */
