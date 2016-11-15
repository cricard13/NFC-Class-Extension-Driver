/*
*          Modifications Copyright (c) Microsoft. All rights reserved.
*
*              Original code Copyright (c), NXP Semiconductors
*/

#include "phNciNfc_Pch.h" 

#include "phNciNfc_Init.tmh"

static NFCSTATUS phNciNfc_Init(void *pContext);
static NFCSTATUS phNciNfc_ProcessInitRsp(void *pContext, NFCSTATUS wStatus);
static NFCSTATUS phNciNfc_CompleteInitSequence(void *pContext, NFCSTATUS wStatus);

static NFCSTATUS phNciNfc_DelayForResetNtfProc(void* pContext, NFCSTATUS wStatus);
static NFCSTATUS phNciNfc_DelayForResetNtfInit(void* pContext);
static NFCSTATUS phNciNfc_DelayForResetNtf(void* pContext);

static NFCSTATUS phNciNfc_InitReset(void *pContext);
static NFCSTATUS phNciNfc_ProcessResetRsp(void *pContext, NFCSTATUS wStatus);
static NFCSTATUS phNciNfc_SendReset(void *pContext);

static NFCSTATUS phNciNfc_CompleteReleaseSequence(void *pContext, NFCSTATUS wStatus);
static NFCSTATUS phNciNfc_CompleteNfccResetSequence(void *pContext, NFCSTATUS wStatus);

static NFCSTATUS phNciNfc_ResetNtfCb(void* pContext, void* pInfo, NFCSTATUS status);

static NFCSTATUS phNciNfc_RegAllNtfs(void* pContext);

/*Global Varibales for Init Sequence Handler*/
phNciNfc_SequenceP_t gphNciNfc_InitSequence[] = {
    {&phNciNfc_DelayForResetNtfInit, &phNciNfc_DelayForResetNtfProc},
    {&phNciNfc_InitReset, &phNciNfc_ProcessResetRsp},
    {&phNciNfc_DelayForResetNtf, &phNciNfc_DelayForResetNtfProc},
    {&phNciNfc_Init, &phNciNfc_ProcessInitRsp},
    {NULL, &phNciNfc_CompleteInitSequence}
};

/*Global Varibales for Release Sequence Handler*/
phNciNfc_SequenceP_t gphNciNfc_ReleaseSequence[] = {
    {&phNciNfc_SendReset, &phNciNfc_ProcessResetRsp},
    {NULL, &phNciNfc_CompleteReleaseSequence}
};

/*Global Varibales for Nfcc reset sequence*/
phNciNfc_SequenceP_t gphNciNfc_NfccResetSequence[] = {
    {&phNciNfc_SendReset, &phNciNfc_ProcessResetRsp},
    {NULL, &phNciNfc_CompleteNfccResetSequence}
};

/** Core Reset notification timeout */
#define PHNCINFC_CORE_RESET_NTF_TIMEOUT        (30)

static NFCSTATUS phNciNfc_Init(void *pContext)
{
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    phNciNfc_CoreTxInfo_t TxInfo;
    pphNciNfc_Context_t pNciContext = (pphNciNfc_Context_t)pContext;

    PH_LOG_NCI_FUNC_ENTRY();
    phOsalNfc_SetMemory(&TxInfo, 0x00, sizeof(phNciNfc_CoreTxInfo_t));
    TxInfo.tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlCmd;
    TxInfo.tHeaderInfo.Group_ID = phNciNfc_e_CoreNciCoreGid;
    TxInfo.tHeaderInfo.Opcode_ID.OidType.NciCoreCmdOid = phNciNfc_e_NciCoreInitCmdOid;
    TxInfo.Buff = (uint8_t *)&pNciContext->tInitInfo.bExtension;
    TxInfo.wLen = 0;

    wStatus = phNciNfc_CoreIfTxRx(&(pNciContext->NciCoreContext),
                                    &TxInfo,
                                    &(pNciContext->RspBuffInfo),
                                    PHNCINFC_NCI_INIT_RSP_TIMEOUT,
                                    (pphNciNfc_CoreIfNtf_t)&phNciNfc_GenericSequence,
                                    pContext);
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS phNciNfc_ProcessInitRsp(void *pContext, NFCSTATUS Status)
{
    NFCSTATUS wStatus = Status;
    phNciNfc_sInitRspParams_t *pInitRsp = NULL;
    pphNciNfc_Context_t pNciContext = pContext;
    uint8_t *pBuff;
    uint8_t Offset = 0;
    uint16_t wTemp = 0;

    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciContext)
    {
        pInitRsp = &(pNciContext->InitRspParams);
        if (pNciContext->RspBuffInfo.wLen >= 19)
        {
            if (pNciContext->RspBuffInfo.pBuff[Offset++] == PH_NCINFC_STATUS_OK)
            {
                wStatus = NFCSTATUS_SUCCESS;

                /*NFCC Features*/
                pInitRsp->NfccFeatures.DiscConfSuprt =  pNciContext->RspBuffInfo.pBuff[Offset++];
                pInitRsp->NfccFeatures.RoutingType =  pNciContext->RspBuffInfo.pBuff[Offset++];
                pInitRsp->NfccFeatures.PwrOffState =  pNciContext->RspBuffInfo.pBuff[Offset++];
                pInitRsp->NfccFeatures.Byte3 =  pNciContext->RspBuffInfo.pBuff[Offset++];

                /*number of supported RF interfaces*/
                pInitRsp->NoOfRfIfSuprt = pNciContext->RspBuffInfo.pBuff[Offset++];

                if(pInitRsp->NoOfRfIfSuprt <= PH_NCINFC_CORE_MAX_SUP_RF_INTFS)
                {
                    /*Supported RF Interfaces */
                    phOsalNfc_MemCopy(pInitRsp->RfInterfaces, &(pNciContext->RspBuffInfo.pBuff[Offset]),
                                                                pInitRsp->NoOfRfIfSuprt);
                    Offset += pInitRsp->NoOfRfIfSuprt;

                    /*Max No of Logical Connection supported*/
                    pInitRsp->MaxLogicalCon = pNciContext->RspBuffInfo.pBuff[Offset++];

                    /*Routing Table Size*/
                    pInitRsp->RoutingTableSize = pNciContext->RspBuffInfo.pBuff[Offset++]; /*LSB*/
                    wTemp = pNciContext->RspBuffInfo.pBuff[Offset++];
                    pInitRsp->RoutingTableSize = (pInitRsp->RoutingTableSize) | (wTemp << 8);

                    /*Control Packet Payload Length*/
                    pInitRsp->CntrlPktPayloadLen = pNciContext->RspBuffInfo.pBuff[Offset++];

                    /*Max Size For Large parameter*/
                    pInitRsp->MaxSizeLarge = pNciContext->RspBuffInfo.pBuff[Offset++]; /*LSB*/
                    wTemp = (uint16_t)pNciContext->RspBuffInfo.pBuff[Offset++];
                    pInitRsp->MaxSizeLarge = (pInitRsp->MaxSizeLarge) | (wTemp << 8);

                    /*Manufacturer ID*/
                    pInitRsp->ManufacturerId = pNciContext->RspBuffInfo.pBuff[Offset++];
                    if(pInitRsp->ManufacturerId != 0x00)
                    {
                        pBuff = (uint8_t *)phOsalNfc_GetMemory(PHNCINFC_CORE_MANUF_INFO_LEN_NCI1x);
                        if (pBuff != NULL)
                        {
                            pNciContext->InitRspParams.ManufacturerInfo.ManufacturerInfoBuffer = pBuff;
                            pNciContext->InitRspParams.ManufacturerInfo.ManufacturerInfoLength =
                                                                    PHNCINFC_CORE_MANUF_INFO_LEN_NCI1x;
                            /*Decided by Manufacturer*/
                            memcpy(pBuff, &pNciContext->RspBuffInfo.pBuff[Offset],
                                                                    PHNCINFC_CORE_MANUF_INFO_LEN_NCI1x);
                        }
                    }

                    wStatus = phNciNfc_CoreIfSetMaxCtrlPacketSize(&(pNciContext->NciCoreContext),
                                                                     pInitRsp->CntrlPktPayloadLen);
                }
                else
                {
                    PH_LOG_NCI_CRIT_STR("Invalid number of supported Rf interfaces");
                    wStatus = NFCSTATUS_FAILED;
                }
            }
            else
            {
                wStatus = NFCSTATUS_FAILED;
            }
        }
        else
        {
            wStatus = NFCSTATUS_FAILED;
        }
    }
    else
    {
        wStatus = NFCSTATUS_INVALID_PARAMETER;
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}


static void phNciNfc_ResetNtfDelayCb(uint32_t dwTimerId, void *pContext)
{
    pphNciNfc_Context_t pNciContext = (pphNciNfc_Context_t)pContext;
    /* Internal event is made failed in order to remain in same state */
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    UNUSED(dwTimerId);
    PH_LOG_LIBNFC_FUNC_ENTRY();
    if (NULL != pNciContext)
    {
        (void)phOsalNfc_Timer_Stop(pNciContext->dwNtfTimerId);
        (void)phOsalNfc_Timer_Delete(pNciContext->dwNtfTimerId);

        pNciContext->dwNtfTimerId = 0;

        wStatus = phNciNfc_GenericSequence(pNciContext, NULL, wStatus);
    }
    PH_LOG_LIBNFC_FUNC_EXIT();
    return;
}

static NFCSTATUS phNciNfc_DelayForResetNtfProc(void* pContext, NFCSTATUS status)
{
    UNUSED(status);
    UNUSED(pContext);
    PH_LOG_LIBNFC_FUNC_ENTRY();
    PH_LOG_LIBNFC_FUNC_EXIT();
    return NFCSTATUS_SUCCESS;
}

NFCSTATUS phNciNfc_DelayForResetNtfInit(void* pContext)
{
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    pphNciNfc_Context_t pNciContext = (pphNciNfc_Context_t)pContext;

    PH_LOG_LIBNFC_FUNC_ENTRY();

    if (0 == pNciContext->tInitInfo.bSkipRegisterAllNtfs)
    {
        wStatus = phNciNfc_RegAllNtfs(pNciContext);
    }

    if (wStatus == NFCSTATUS_SUCCESS)
    {
        wStatus = phNciNfc_DelayForResetNtf(pContext);
    }
    PH_LOG_LIBNFC_FUNC_EXIT();
    return wStatus;
}

NFCSTATUS phNciNfc_DelayForResetNtf(void* pContext)
{
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    pphNciNfc_Context_t pNciContext = (pphNciNfc_Context_t)pContext;

    PH_LOG_LIBNFC_FUNC_ENTRY();
    if (NULL != pNciContext)
    {
        PH_LOG_LIBNFC_CRIT_U32MSG("Delay to receive Core Reset ntf", PHNCINFC_CORE_RESET_NTF_TIMEOUT);

        pNciContext->dwNtfTimerId = phOsalNfc_Timer_Create();
        if (PH_OSALNFC_TIMER_ID_INVALID != pNciContext->dwNtfTimerId)
        {
            wStatus = phOsalNfc_Timer_Start(pNciContext->dwNtfTimerId,
                                            PHNCINFC_CORE_RESET_NTF_TIMEOUT,
                                            &phNciNfc_ResetNtfDelayCb,
                                            (void *)pNciContext);
            if (NFCSTATUS_SUCCESS == wStatus)
            {
                wStatus = NFCSTATUS_PENDING;
            }
            else
            {
                (void)phOsalNfc_Timer_Delete(pNciContext->dwNtfTimerId);
                pNciContext->dwNtfTimerId = 0;
                wStatus = NFCSTATUS_FAILED;
            }
        }
    }
    PH_LOG_LIBNFC_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS phNciNfc_InitReset(void *pContext)
{
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    phNciNfc_CoreTxInfo_t TxInfo;
    pphNciNfc_Context_t pNciContext = (pphNciNfc_Context_t)pContext;

    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciContext)
    {
        phOsalNfc_SetMemory(&TxInfo, 0x00, sizeof(phNciNfc_CoreTxInfo_t));
        TxInfo.tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlCmd;
        TxInfo.tHeaderInfo.Group_ID = phNciNfc_e_CoreNciCoreGid;
        TxInfo.tHeaderInfo.Opcode_ID.OidType.NciCoreCmdOid = phNciNfc_e_NciCoreResetCmdOid;
        TxInfo.Buff = (uint8_t *)&pNciContext->ResetInfo.ResetTypeReq;
        TxInfo.wLen = 1;
        wStatus = phNciNfc_CoreIfTxRx(&(pNciContext->NciCoreContext),
                                    &TxInfo,
                                    &(pNciContext->RspBuffInfo),
                                    PHNCINFC_NCI_CMD_RSP_TIMEOUT,
                                    (pphNciNfc_CoreIfNtf_t)&phNciNfc_GenericSequence,
                                    pContext);
    }
    else
    {
        wStatus = NFCSTATUS_INVALID_PARAMETER;
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS phNciNfc_ProcessResetRsp(void *pContext, NFCSTATUS Status)
{
    NFCSTATUS wStatus = Status;
    pphNciNfc_Context_t pNciContext = pContext;

    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciContext)
    {
        if(NFCSTATUS_RESPONSE_TIMEOUT != Status)
        {
            if (pNciContext->RspBuffInfo.wLen == 3)
            {
                /*Check Status Byte*/
                if (pNciContext->RspBuffInfo.pBuff[0] == PH_NCINFC_STATUS_OK)
                {
                    /* Nfcc supported Nci version */
                    pNciContext->ResetInfo.NciVer = pNciContext->RspBuffInfo.pBuff[1];

                    if ((pNciContext->ResetInfo.NciVer & PH_NCINFC_VERSION_MAJOR_MASK) <=
                        (PH_NCI1xNFC_VERSION & PH_NCINFC_VERSION_MAJOR_MASK))
                    {
                        /* Update Reset type */
                        if (pNciContext->RspBuffInfo.pBuff[2] == phNciNfc_ResetType_KeepConfig)
                        {
                            PH_LOG_NCI_INFO_STR("Nfcc reseted to 'phNciNfc_ResetType_KeepConfig'");
                            pNciContext->ResetInfo.ResetTypeRsp = phNciNfc_ResetType_KeepConfig;
                        }
                        else
                        {
                            PH_LOG_NCI_INFO_STR("Nfcc reseted to 'phNciNfc_ResetType_ResetConfig'");
                            pNciContext->ResetInfo.ResetTypeRsp = phNciNfc_ResetType_ResetConfig;
                        }

                        wStatus = NFCSTATUS_SUCCESS;
                    }
                    else
                    {
                        PH_LOG_NCI_INFO_STR("Unsupported NCI version 0x%02x", pNciContext->ResetInfo.NciVer);
                        wStatus = NFCSTATUS_FAILED;
                    }
                }
                else
                {
                    wStatus = NFCSTATUS_FAILED;
                }
            }
            else if (pNciContext->RspBuffInfo.wLen == 1)
            {
                /*Check Status Byte*/
                if (pNciContext->RspBuffInfo.pBuff[0] == PH_NCINFC_STATUS_OK)
                {
                    if ((pNciContext->ResetInfo.NciVer & PH_NCINFC_VERSION_MAJOR_MASK) ==
                        (PH_NCI2xNFC_VERSION & PH_NCINFC_VERSION_MAJOR_MASK))
                    {
                        wStatus = NFCSTATUS_SUCCESS;
                    }
                    else
                    {
                        PH_LOG_NCI_INFO_STR("Unsupported NCI version 0x%02x", pNciContext->ResetInfo.NciVer);
                        wStatus = NFCSTATUS_FAILED;
                    }
                }
                else
                {
                    wStatus = NFCSTATUS_FAILED;
                }
            }
            else
            {
                wStatus = NFCSTATUS_INVALID_PARAMETER;
            }
        }else
        {
            wStatus = NFCSTATUS_FAILED;
        }
    }else
    {
        wStatus = NFCSTATUS_INVALID_PARAMETER;
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS phNciNfc_CompleteInitSequence(void *pContext, NFCSTATUS wStatus)
{
    pphNciNfc_Context_t pNciCtx = pContext;
    phNciNfc_TransactInfo_t tTranscInfo;
    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciCtx)
    {
        tTranscInfo.pContext = (void*)pNciCtx;
        tTranscInfo.pbuffer = (void*)&pNciCtx->ResetInfo.ResetTypeRsp;
        tTranscInfo.wLength = sizeof(pNciCtx->ResetInfo.ResetTypeRsp);
        phNciNfc_Notify(pNciCtx, wStatus,(void *)&tTranscInfo);
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS phNciNfc_SendReset(void *pContext)
{
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    phNciNfc_CoreTxInfo_t TxInfo;
    pphNciNfc_Context_t pNciContext = (pphNciNfc_Context_t)pContext;

    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciContext)
    {
        phOsalNfc_SetMemory(&TxInfo, 0x00, sizeof(phNciNfc_CoreTxInfo_t));
        TxInfo.tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlCmd;
        TxInfo.tHeaderInfo.Group_ID = phNciNfc_e_CoreNciCoreGid;
        TxInfo.tHeaderInfo.Opcode_ID.OidType.NciCoreCmdOid = phNciNfc_e_NciCoreResetCmdOid;
        TxInfo.Buff = (uint8_t *)&pNciContext->ResetInfo.ResetTypeReq;
        TxInfo.wLen = 1;
        wStatus = phNciNfc_CoreIfTxRx(&(pNciContext->NciCoreContext), &TxInfo,
            &(pNciContext->RspBuffInfo), PHNCINFC_NCI_CMD_RSP_TIMEOUT,
            (pphNciNfc_CoreIfNtf_t)&phNciNfc_GenericSequence, pContext);
    }
    else
    {
        wStatus = NFCSTATUS_INVALID_PARAMETER;
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS
phNciNfc_CompleteReleaseSequence(void *pContext, NFCSTATUS wStatus)
{
    pphNciNfc_Context_t pNciCtx = pContext;
    pphNciNfc_IfNotificationCb_t pUpperLayerCb = NULL;
    void *pUpperLayerCtx = NULL;

    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciCtx)
    {
        pUpperLayerCb = pNciCtx->IfNtf;
        pUpperLayerCtx = pNciCtx->IfNtfCtx;
        pNciCtx->IfNtf = NULL;
        pNciCtx->IfNtfCtx = NULL;

        wStatus = phNciNfc_ReleaseNciHandle();

        if(NULL != pUpperLayerCb)
        {
            PH_LOG_NCI_INFO_STR("Invoking upper layer call back function");
            pUpperLayerCb(pUpperLayerCtx, wStatus, NULL);
        }
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS
phNciNfc_CompleteNfccResetSequence(void *pContext, NFCSTATUS wStatus)
{
    pphNciNfc_Context_t pNciCtx = pContext;

    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pNciCtx)
    {
        phNciNfc_Notify(pNciCtx, wStatus,NULL);
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS
phNciNfc_ResetNtfCb(void*     pContext,
                    void *pInfo,
                    NFCSTATUS status)
{
    pphNciNfc_Context_t pNciCtx = (pphNciNfc_Context_t )pContext;
    pphNciNfc_TransactInfo_t pTransInfo = pInfo;
    uint8_t wDataLen;
    uint8_t *pBuff;
    NFCSTATUS wStatus;

    wStatus  = status;

    PH_LOG_NCI_FUNC_ENTRY();
    if((NULL != pNciCtx) && (NULL != pTransInfo))
    {
        /* Reset notification received, take necessary action */
        PH_LOG_NCI_INFO_STR("Received RESET notification from NFCC");

        if (pNciCtx->dwNtfTimerId != 0)
        {
            (void)phOsalNfc_Timer_Stop(pNciCtx->dwNtfTimerId);
            (void)phOsalNfc_Timer_Delete(pNciCtx->dwNtfTimerId);

            pNciCtx->dwNtfTimerId = 0;

            if (pTransInfo->wLength >= 5)
            {
                /* Nfcc supported Nci version */
                pNciCtx->ResetInfo.NciVer = pTransInfo->pbuffer[2];
                if ((pNciCtx->ResetInfo.NciVer & PH_NCINFC_VERSION_MAJOR_MASK) ==
                    (PH_NCI2xNFC_VERSION & PH_NCINFC_VERSION_MAJOR_MASK))
                {
                    /* Update Reset type */
                    if (pTransInfo->pbuffer[1] == phNciNfc_ResetType_KeepConfig)
                    {
                        PH_LOG_NCI_INFO_STR("Nfcc reseted to 'phNciNfc_ResetType_KeepConfig'");
                        pNciCtx->ResetInfo.ResetTypeRsp = phNciNfc_ResetType_KeepConfig;
                    }
                    else
                    {
                        PH_LOG_NCI_INFO_STR("Nfcc reseted to 'phNciNfc_ResetType_ResetConfig'");
                        pNciCtx->ResetInfo.ResetTypeRsp = phNciNfc_ResetType_ResetConfig;
                    }

                    /*Manufacturer ID*/
                    pNciCtx->InitRspParams.ManufacturerId = pTransInfo->pbuffer[3];
                    if (pNciCtx->InitRspParams.ManufacturerId != 0x00)
                    {
                        wDataLen = pTransInfo->pbuffer[4];
                        if (wDataLen == pTransInfo->wLength - 5)
                        {
                            pBuff = (uint8_t *)phOsalNfc_GetMemory(wDataLen);
                            if (pBuff != NULL)
                            {
                                pNciCtx->InitRspParams.ManufacturerInfo.ManufacturerInfoBuffer = pBuff;
                                pNciCtx->InitRspParams.ManufacturerInfo.ManufacturerInfoLength = wDataLen;
                                memcpy(pBuff, &pTransInfo->pbuffer[5], wDataLen);

                                /*Reset Trigger*/
                                if (pTransInfo->pbuffer[0] == 0) /*If error is unrecoverable*/
                                    wStatus = NFCSTATUS_FAILED;
                                else
                                    wStatus = NFCSTATUS_SUCCESS;
                            }
                            else
                            {
                                wStatus = NFCSTATUS_FAILED;
                            }
                        }
                    }
                }
                else
                {
                    PH_LOG_NCI_INFO_STR("Unsupported NCI version 0x%02x", pNciCtx->ResetInfo.NciVer);
                    wStatus = NFCSTATUS_FAILED;
                }
            }
            else
            {
                wStatus = NFCSTATUS_INVALID_PARAMETER;
            }

            wStatus = phNciNfc_GenericSequence(pNciCtx, pInfo, status);
        }
        else
        {
            /* Reset Sender statemachine */
            (void)phNciNfc_CoreResetSenderStateMachine(&pNciCtx->NciCoreContext);
            (void)phTmlNfc_WriteAbort(pNciCtx->NciCoreContext.pHwRef);

            if (NULL != pNciCtx->tRegListInfo.pResetNtfCb)
            {
                pNciCtx->tRegListInfo.pResetNtfCb(pNciCtx->tRegListInfo.ResetNtfCtxt,
                        eNciNfc_NciResetNtf, pInfo, NFCSTATUS_SUCCESS);
            }
        }
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}

static NFCSTATUS phNciNfc_RegAllNtfs(void*     pContext)
{
    NFCSTATUS wStatus = NFCSTATUS_SUCCESS;
    pphNciNfc_Context_t pCtx = pContext;
    phNciNfc_sCoreHeaderInfo_t tHeaderInfo;
    PH_LOG_NCI_FUNC_ENTRY();
    if(NULL != pCtx)
    {
        /*Register Notification function for Rf field info*/
        tHeaderInfo.Group_ID = phNciNfc_e_CoreRfMgtGid;
        tHeaderInfo.Opcode_ID.OidType.RfMgtNtfOid = phNciNfc_e_RfMgtRfFieldInfoNtfOid;
        tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
        wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                            &(tHeaderInfo),
                                            &phNciNfc_RfFieldInfoNtfHandler,
                                            pContext
                                           );
        /*Register Notification function for Rf-Nfcee Action info*/
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Rf-Nfcee Action Notification");
            tHeaderInfo.Group_ID = phNciNfc_e_CoreRfMgtGid;
            tHeaderInfo.Opcode_ID.OidType.RfMgtNtfOid = phNciNfc_e_RfMgtRfNfceeActionNtfOid;
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                        &(tHeaderInfo),
                                        &phNciNfc_NfceeActionNtfHandler,
                                        pContext
                                       );
        }
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            /*Register Notification function for Rf Deactivate info*/
            tHeaderInfo.Group_ID = phNciNfc_e_CoreRfMgtGid;
            tHeaderInfo.Opcode_ID.OidType.RfMgtNtfOid = phNciNfc_e_RfMgtRfDeactivateNtfOid;
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                                &(tHeaderInfo),
                                                &phNciNfc_ProcessDeActvNtf,
                                                pContext
                                               );
        }
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Interface Activated Notification");
            /* Register for Interface Activation Notification */
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            tHeaderInfo.Group_ID = phNciNfc_e_CoreRfMgtGid;
            tHeaderInfo.Opcode_ID.OidType.RfMgtNtfOid = phNciNfc_e_RfMgtRfIntfActivatedNtfOid;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                                &(tHeaderInfo),
                                                &phNciNfc_ProcessActvNtf,
                                                pContext
                                               );
        }
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Discover Notification");
            /* Register for Interface Activation Notification */
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            tHeaderInfo.Group_ID = phNciNfc_e_CoreRfMgtGid;
            tHeaderInfo.Opcode_ID.OidType.RfMgtNtfOid = phNciNfc_e_RfMgtRfDiscoverNtfOid;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                                &(tHeaderInfo),
                                                &phNciNfc_ProcessDiscNtf,
                                                pContext
                                               );
        }
        /* Register for Interface error notification */
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Interface Error Notification");
            /* Register for Interface Activation Notification */
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            tHeaderInfo.Group_ID = phNciNfc_e_CoreNciCoreGid;
            tHeaderInfo.Opcode_ID.OidType.NciCoreNtfOid = phNciNfc_e_NciCoreInterfaceErrNtfOid;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                                &(tHeaderInfo),
                                                &phNciNfc_ProcessIntfErrNtf,
                                                pContext
                                               );
        }
        /* Register for Generic error notification */
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Generic Error Notification");
            /* Register for Interface Activation Notification */
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            tHeaderInfo.Group_ID = phNciNfc_e_CoreNciCoreGid;
            tHeaderInfo.Opcode_ID.OidType.NciCoreNtfOid = phNciNfc_e_NciCoreGenericErrNtfOid;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                                &(tHeaderInfo),
                                                &phNciNfc_ProcessGenericErrNtf,
                                                pContext
                                               );
        }
        /* Register for Reset notification */
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Reset Notification");
            /* Register for Interface Activation Notification */
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            tHeaderInfo.Group_ID = phNciNfc_e_CoreNciCoreGid;
            tHeaderInfo.Opcode_ID.OidType.NciCoreNtfOid = phNciNfc_e_NciCoreResetNtfOid;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                                &(tHeaderInfo),
                                                &phNciNfc_ResetNtfCb,
                                                pContext
                                               );
        }
        /*Register for Rf Nfcee Discovery Request Notification */
        if(NFCSTATUS_SUCCESS == wStatus)
        {
            PH_LOG_NCI_INFO_STR("Registering for Rf Nfcee Discovery Request Notification");
            tHeaderInfo.Group_ID = phNciNfc_e_CoreRfMgtGid;
            tHeaderInfo.Opcode_ID.OidType.RfMgtNtfOid = phNciNfc_e_RfMgtRfNfceeDiscoveryReqNtfOid;
            tHeaderInfo.eMsgType = phNciNfc_e_NciCoreMsgTypeCntrlNtf;
            wStatus = phNciNfc_CoreIfRegRspNtf(&(pCtx->NciCoreContext),
                                        &(tHeaderInfo),
                                        &phNciNfc_NfceeDiscReqNtfHandler,
                                        pContext
                                       );
        }
    }
    else
    {
        wStatus = NFCSTATUS_INVALID_PARAMETER;
    }
    PH_LOG_NCI_FUNC_EXIT();
    return wStatus;
}
