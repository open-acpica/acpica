/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
 *
 *****************************************************************************/

/******************************************************************************
 *
 * Copyright (c) 1999 - 2026, Intel Corp.
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only
 *
 *****************************************************************************/

#include "acpi.h"
#include "accommon.h"
#include "acevents.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_EVENTS
        ACPI_MODULE_NAME    ("evgpe")

#if (!ACPI_REDUCED_HARDWARE) /* Entire module */

/* Local prototypes */

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchExecuteGpeMethod (
    void                    *Context);

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchEnableGpe (
    void                    *Context);


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvUpdateGpeEnableMask
 *
 * PARAMETERS:  GpeEventInfo            - GPE to update
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Updates GPE register enable mask based upon whether there are
 *              runtime references to this GPE
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvUpdateGpeEnableMask (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    UINT32                  RegisterBit;


    ACPI_FUNCTION_TRACE (EvUpdateGpeEnableMask);


    GpeRegisterInfo = GpeEventInfo->RegisterInfo;
    if (!GpeRegisterInfo)
    {
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* Clear the run bit up front */

    ACPI_CLEAR_BIT (GpeRegisterInfo->EnableForRun, RegisterBit);

    /* Set the mask bit only if there are references to this GPE */

    if (GpeEventInfo->RuntimeCount)
    {
        ACPI_SET_BIT (GpeRegisterInfo->EnableForRun, (UINT8) RegisterBit);
    }

    GpeRegisterInfo->EnableMask = GpeRegisterInfo->EnableForRun;
    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvEnableGpe
 *
 * PARAMETERS:  GpeEventInfo            - GPE to enable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable a GPE.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvEnableGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (EvEnableGpe);


    /* Enable the requested GPE */

    Status = AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_ENABLE);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvMaskGpe
 *
 * PARAMETERS:  GpeEventInfo            - GPE to be blocked/unblocked
 *              IsMasked                - Whether the GPE is masked or not
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unconditionally mask/unmask a GPE during runtime.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvMaskGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    BOOLEAN                 IsMasked)
{
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    UINT32                  RegisterBit;


    ACPI_FUNCTION_TRACE (EvMaskGpe);


    GpeRegisterInfo = GpeEventInfo->RegisterInfo;
    if (!GpeRegisterInfo)
    {
        return_ACPI_STATUS (AE_NOT_EXIST);
    }

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* Perform the action */

    if (IsMasked)
    {
        if (RegisterBit & GpeRegisterInfo->MaskForRun)
        {
            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
        ACPI_SET_BIT (GpeRegisterInfo->MaskForRun, (UINT8) RegisterBit);
    }
    else
    {
        if (!(RegisterBit & GpeRegisterInfo->MaskForRun))
        {
            return_ACPI_STATUS (AE_BAD_PARAMETER);
        }

        ACPI_CLEAR_BIT (GpeRegisterInfo->MaskForRun, (UINT8) RegisterBit);
        if (GpeEventInfo->RuntimeCount &&
            !GpeEventInfo->DisableForDispatch)
        {
            (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_ENABLE);
        }
    }

    return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvAddGpeReference
 *
 * PARAMETERS:  GpeEventInfo            - Add a reference to this GPE
 *              ClearOnEnable           - Clear GPE status before enabling it
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a reference to a GPE. On the first reference, the GPE is
 *              hardware-enabled.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvAddGpeReference (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    BOOLEAN                 ClearOnEnable)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (EvAddGpeReference);


    if (GpeEventInfo->RuntimeCount == ACPI_UINT8_MAX)
    {
        return_ACPI_STATUS (AE_LIMIT);
    }

    GpeEventInfo->RuntimeCount++;
    if (GpeEventInfo->RuntimeCount == 1)
    {
        /* Enable on first reference */

        if (ClearOnEnable)
        {
            (void) AcpiHwClearGpe (GpeEventInfo);
        }

        Status = AcpiEvUpdateGpeEnableMask (GpeEventInfo);
        if (ACPI_SUCCESS (Status))
        {
            Status = AcpiEvEnableGpe (GpeEventInfo);
        }

        if (ACPI_FAILURE (Status))
        {
            GpeEventInfo->RuntimeCount--;
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvRemoveGpeReference
 *
 * PARAMETERS:  GpeEventInfo            - Remove a reference to this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a reference to a GPE. When the last reference is
 *              removed, the GPE is hardware-disabled.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvRemoveGpeReference (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status = AE_OK;


    ACPI_FUNCTION_TRACE (EvRemoveGpeReference);


    if (!GpeEventInfo->RuntimeCount)
    {
        return_ACPI_STATUS (AE_LIMIT);
    }

    GpeEventInfo->RuntimeCount--;
    if (!GpeEventInfo->RuntimeCount)
    {
        /* Disable on last reference */

        Status = AcpiEvUpdateGpeEnableMask (GpeEventInfo);
        if (ACPI_SUCCESS (Status))
        {
            Status = AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
        }

        if (ACPI_FAILURE (Status))
        {
            GpeEventInfo->RuntimeCount++;
        }
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvLowGetGpeInfo
 *
 * PARAMETERS:  GpeNumber           - Raw GPE number
 *              GpeBlock            - A GPE info block
 *
 * RETURN:      A GPE EventInfo struct. NULL if not a valid GPE (The GpeNumber
 *              is not within the specified GPE block)
 *
 * DESCRIPTION: Returns the EventInfo struct associated with this GPE. This is
 *              the low-level implementation of EvGetGpeEventInfo.
 *
 ******************************************************************************/

ACPI_GPE_EVENT_INFO *
AcpiEvLowGetGpeInfo (
    UINT32                  GpeNumber,
    ACPI_GPE_BLOCK_INFO     *GpeBlock)
{
    UINT32                  GpeIndex;


    /*
     * Validate that the GpeNumber is within the specified GpeBlock.
     * (Two steps)
     */
    if (!GpeBlock ||
        (GpeNumber < GpeBlock->BlockBaseNumber))
    {
        return (NULL);
    }

    GpeIndex = GpeNumber - GpeBlock->BlockBaseNumber;
    if (GpeIndex >= GpeBlock->GpeCount)
    {
        return (NULL);
    }

    return (&GpeBlock->EventInfo[GpeIndex]);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGetGpeEventInfo
 *
 * PARAMETERS:  GpeDevice           - Device node. NULL for GPE0/GPE1
 *              GpeNumber           - Raw GPE number
 *
 * RETURN:      A GPE EventInfo struct. NULL if not a valid GPE
 *
 * DESCRIPTION: Returns the EventInfo struct associated with this GPE.
 *              Validates the GpeBlock and the GpeNumber
 *
 *              Should be called only when the GPE lists are semaphore locked
 *              and not subject to change.
 *
 ******************************************************************************/

ACPI_GPE_EVENT_INFO *
AcpiEvGetGpeEventInfo (
    ACPI_HANDLE             GpeDevice,
    UINT32                  GpeNumber)
{
    ACPI_OPERAND_OBJECT     *ObjDesc;
    ACPI_GPE_EVENT_INFO     *GpeInfo;
    UINT32                  i;


    ACPI_FUNCTION_ENTRY ();


    /* A NULL GpeDevice means use the FADT-defined GPE block(s) */

    if (!GpeDevice)
    {
        /* Examine GPE Block 0 and 1 (These blocks are permanent) */

        for (i = 0; i < ACPI_MAX_GPE_BLOCKS; i++)
        {
            GpeInfo = AcpiEvLowGetGpeInfo (GpeNumber,
                AcpiGbl_GpeFadtBlocks[i]);
            if (GpeInfo)
            {
                return (GpeInfo);
            }
        }

        /* The GpeNumber was not in the range of either FADT GPE block */

        return (NULL);
    }

    /* A Non-NULL GpeDevice means this is a GPE Block Device */

    ObjDesc = AcpiNsGetAttachedObject ((ACPI_NAMESPACE_NODE *) GpeDevice);
    if (!ObjDesc ||
        !ObjDesc->Device.GpeBlock)
    {
        return (NULL);
    }

    return (AcpiEvLowGetGpeInfo (GpeNumber, ObjDesc->Device.GpeBlock));
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGpeDetect
 *
 * PARAMETERS:  GpeXruptList        - Interrupt block for this interrupt.
 *                                    Can have multiple GPE blocks attached.
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect if any GP events have occurred. This function is
 *              executed at interrupt level.
 *
 ******************************************************************************/

UINT32
AcpiEvGpeDetect (
    ACPI_GPE_XRUPT_INFO     *GpeXruptList)
{
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_NAMESPACE_NODE     *GpeDevice;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_GPE_EVENT_INFO     *GpeEventInfo;
    UINT32                  GpeNumber;
    UINT32                  IntStatus = ACPI_INTERRUPT_NOT_HANDLED;
    ACPI_CPU_FLAGS          Flags;
    UINT32                  i;
    UINT32                  j;


    ACPI_FUNCTION_NAME (EvGpeDetect);

    /* Check for the case where there are no GPEs */

    if (!GpeXruptList)
    {
        return (IntStatus);
    }

    /*
     * We need to obtain the GPE lock for both the data structs and registers
     * Note: Not necessary to obtain the hardware lock, since the GPE
     * registers are owned by the GpeLock.
     */
    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);

    /* Examine all GPE blocks attached to this interrupt level */

    GpeBlock = GpeXruptList->GpeBlockListHead;
    while (GpeBlock)
    {
        GpeDevice = GpeBlock->Node;

        /*
         * Read all of the 8-bit GPE status and enable registers in this GPE
         * block, saving all of them. Find all currently active GP events.
         */
        for (i = 0; i < GpeBlock->RegisterCount; i++)
        {
            /* Get the next status/enable pair */

            GpeRegisterInfo = &GpeBlock->RegisterInfo[i];

            /*
             * Optimization: If there are no GPEs enabled within this
             * register, we can safely ignore the entire register.
             */
            if (!(GpeRegisterInfo->EnableForRun |
                  GpeRegisterInfo->EnableForWake))
            {
                ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
                    "Ignore disabled registers for GPE %02X-%02X: "
                    "RunEnable=%02X, WakeEnable=%02X\n",
                    GpeRegisterInfo->BaseGpeNumber,
                    GpeRegisterInfo->BaseGpeNumber + (ACPI_GPE_REGISTER_WIDTH - 1),
                    GpeRegisterInfo->EnableForRun,
                    GpeRegisterInfo->EnableForWake));
                continue;
            }

            /* Now look at the individual GPEs in this byte register */

            for (j = 0; j < ACPI_GPE_REGISTER_WIDTH; j++)
            {
                /* Detect and dispatch one GPE bit */

                GpeEventInfo = &GpeBlock->EventInfo[((ACPI_SIZE) i *
                    ACPI_GPE_REGISTER_WIDTH) + j];
                GpeNumber = j + GpeRegisterInfo->BaseGpeNumber;
                AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
                IntStatus |= AcpiEvDetectGpe (
                    GpeDevice, GpeEventInfo, GpeNumber);
                Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
            }
        }

        GpeBlock = GpeBlock->Next;
    }

    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
    return (IntStatus);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvAsynchExecuteGpeMethod
 *
 * PARAMETERS:  Context (GpeEventInfo) - Info for this GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Perform the actual execution of a GPE control method. This
 *              function is called from an invocation of AcpiOsExecute and
 *              therefore does NOT execute at interrupt level - so that
 *              the control method itself is not executed in the context of
 *              an interrupt handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchExecuteGpeMethod (
    void                    *Context)
{
    ACPI_GPE_EVENT_INFO     *GpeEventInfo = Context;
    ACPI_STATUS             Status = AE_OK;
    ACPI_EVALUATE_INFO      *Info;
    ACPI_GPE_NOTIFY_INFO    *Notify;


    ACPI_FUNCTION_TRACE (EvAsynchExecuteGpeMethod);


    /* Do the correct dispatch - normal method or implicit notify */

    switch (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags))
    {
    case ACPI_GPE_DISPATCH_NOTIFY:
        /*
         * Implicit notify.
         * Dispatch a DEVICE_WAKE notify to the appropriate handler.
         * NOTE: the request is queued for execution after this method
         * completes. The notify handlers are NOT invoked synchronously
         * from this thread -- because handlers may in turn run other
         * control methods.
         *
         * June 2012: Expand implicit notify mechanism to support
         * notifies on multiple device objects.
         */
        Notify = GpeEventInfo->Dispatch.NotifyList;
        while (ACPI_SUCCESS (Status) && Notify)
        {
            Status = AcpiEvQueueNotifyRequest (
                Notify->DeviceNode, ACPI_NOTIFY_DEVICE_WAKE);

            Notify = Notify->Next;
        }
        break;

    case ACPI_GPE_DISPATCH_METHOD:

        /* Allocate the evaluation information block */

        Info = ACPI_ALLOCATE_ZEROED (sizeof (ACPI_EVALUATE_INFO));
        if (!Info)
        {
            Status = AE_NO_MEMORY;
        }
        else
        {
            /*
             * Invoke the GPE Method (_Lxx, _Exx) i.e., evaluate the
             * _Lxx/_Exx control method that corresponds to this GPE
             */
            Info->PrefixNode = GpeEventInfo->Dispatch.MethodNode;
            Info->Flags = ACPI_IGNORE_RETURN_VALUE;

            Status = AcpiNsEvaluate (Info);
            ACPI_FREE (Info);
        }

        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "while evaluating GPE method [%4.4s]",
                AcpiUtGetNodeName (GpeEventInfo->Dispatch.MethodNode)));
        }
        break;

    default:

        goto ErrorExit; /* Should never happen */
    }

    /* Defer enabling of GPE until all notify handlers are done */

    Status = AcpiOsExecute (OSL_NOTIFY_HANDLER,
        AcpiEvAsynchEnableGpe, GpeEventInfo);
    if (ACPI_SUCCESS (Status))
    {
        return_VOID;
    }

ErrorExit:
    AcpiEvAsynchEnableGpe (GpeEventInfo);
    return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiEvNotifyListHasOwner
 *
 * PARAMETERS:  Notify          - List of notify dispatch events
 *              OwnerId         - Table OwnerId being unloaded that should match
 *                                one of the notify device nodes on the list
 *
 * RETURN:      Return TRUE if at least one of the device nodes matches the
 *              passed OwnerId. Return FALSE if all notify list objects don't
 *              match the passed OwnerId.
 *
 * DESCRIPTION: Check whether at least on object on the notify list matches its
 *              OwnerId to the one passed as a function parameter. That means
 *              that at least one device has ownership over an object on this
 *              notify list.
 *
 ******************************************************************************/
static BOOLEAN
AcpiEvNotifyListHasOwner (
    ACPI_GPE_NOTIFY_INFO    *Notify,
    ACPI_OWNER_ID           OwnerId)
{
    while (Notify)
    {
        if (Notify->DeviceNode->OwnerId == OwnerId)
        {
            return (TRUE);
        }
        Notify = Notify->Next;
    }
    return (FALSE);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiEvUnlinkNotifyListDevId
 *
 * PARAMETERS:  OwnerId         - Table OwnerId being unloaded that should match
 *                                the devices on the NotifyList.
 *              NotifyList      - Address of the NotifyList from the GPE
 *              ToFreeList      - Address of a ACPI_GPE_NOTIFY_INFO pointer that
 *                                will later be used to free the device objects
 *                                from the list that match the OwnerId
 *
 * RETURN:      Return TRUE if the original NotifyList was emptied after the
 *              unlinking is done. Return FALSE otherwise.
 *
 * DESCRIPTION: Helper in processing the NotifyList during table unloading.
 *              NotifyList can contain devices from other tables. Only devices
 *              from the table currently getting unloaded should be cleared from
 *              this list. This helper parses the list and moves nodes that
 *              match the unloaded table's ID to a separate single linked list.
 *              That list shall be later freed node by node. It shouldn't be
 *              freed immediately as freeing/allocating should be avoided on
 *              some system when the global spinlock is held.
 *
 ******************************************************************************/
static BOOLEAN
AcpiEvUnlinkNotifyListDevId (
    ACPI_OWNER_ID           OwnerId,
    ACPI_GPE_NOTIFY_INFO  **NotifyList,
    ACPI_GPE_NOTIFY_INFO  **ToFreeList)
{
    /* Cursor pointer that will move through all NotifyList nodes addresses */
    ACPI_GPE_NOTIFY_INFO  **NotifyPointer = NotifyList;
    /* Currently looked at structure in the loop iteration */
    ACPI_GPE_NOTIFY_INFO   *Node = *NotifyPointer;

    /*
     * Go through all Notify structures to find devices matching the OwnerId
     * that is getting unloaded
     */
    while (Node)
    {
        if (Node->DeviceNode->OwnerId == OwnerId)
        {
            /* Modify original list by unlinking Node */
            *NotifyPointer = Node->Next;
            /* Link the unlinked node to the to-be-freed list */
            Node->Next = *ToFreeList;
            *ToFreeList = Node;
        }
        else
        {
            /* Move the cursor one node further */
            NotifyPointer = &Node->Next;
        }
        /* Grab the next node or NULL if there is no more */
        Node = *NotifyPointer;
    }

    /*
     * If the original list was emptied return TRUE so that the caller knows to
     * move the dispatch type from Notify to None
     */
    return (*NotifyList == NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDispatchTypeHiddenHandler
 *
 * PARAMETERS:  GpeEventInfo    - Struct where GPE dispatch type is stored
 *              OwnerId         - Table OwnerId being unloaded that should match
 *                                the GpeEventInfo
 *
 * RETURN:      Return the dispatch type if it is either explicitly present or
 *              hidden behind the handler saved MethodNode. The GPE has to also
 *              match the table ID passed through OwnerId to this function.
 *              Return 0 if the above criteria aren't matched.
 *
 * DESCRIPTION: Check whether a passed GPE event info is of either DISPATCH,
 *              NOTIFY or HANDLER type. Then verify it belongs to the table
 *              ID passed as an argument and that it has a Method Node. Helper
 *              is used for example with cleaning up after table unloading.
 *
 ******************************************************************************/
static UINT8
AcpiEvDispatchTypeHiddenHandler(
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    ACPI_OWNER_ID            OwnerId)
{
    UINT8 GpeDispatchType =  ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags);
    ACPI_GPE_HANDLER_INFO   *Handler;
    ACPI_GPE_NOTIFY_INFO    *Notify;

    switch(GpeDispatchType)
    {
    case ACPI_GPE_DISPATCH_METHOD:
        if(!(GpeEventInfo->Dispatch.MethodNode) ||
            (GpeEventInfo->Dispatch.MethodNode->OwnerId != OwnerId))
        {
            return 0;
        }
        break;

    case ACPI_GPE_DISPATCH_NOTIFY:
        Notify = GpeEventInfo->Dispatch.NotifyList;
        if (!AcpiEvNotifyListHasOwner(Notify, OwnerId))
        {
            return 0;
        }
        break;

    /* Method or Notify can be saved if the handler is present */
    case ACPI_GPE_DISPATCH_HANDLER:
    case ACPI_GPE_DISPATCH_RAW_HANDLER:
        Handler = GpeEventInfo->Dispatch.Handler;
        if (!(Handler->MethodNode))
        {
            return 0;
        }

        switch (ACPI_GPE_DISPATCH_TYPE (Handler->OriginalFlags))
        {
        case ACPI_GPE_DISPATCH_METHOD:
            if (Handler->MethodNode->OwnerId != OwnerId)
            {
                return 0;
            }
            break;
        case ACPI_GPE_DISPATCH_NOTIFY:
            if (!AcpiEvNotifyListHasOwner(
                ACPI_CAST_PTR(ACPI_GPE_NOTIFY_INFO, Handler->MethodNode),
                OwnerId))
            {
                return 0;
            }
            break;
        default:
            return 0;
        }

        break;

    default:
        return 0;
    }
    return GpeDispatchType;
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiEvClearGpeDispatchByOwner
 *
 * PARAMETERS:  OwnerId         - Table OwnerId being unloaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear GPE dispatch info for methods belonging to the table
 *              being unloaded. If any matching GPE is currently dispatching,
 *              it is not cleared and AE_ABORT_METHOD is returned.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvClearGpeDispatchByOwner (
    ACPI_OWNER_ID           OwnerId)
{
    ACPI_GPE_XRUPT_INFO     *GpeXruptInfo;
    ACPI_GPE_BLOCK_INFO     *GpeBlock;
    ACPI_GPE_EVENT_INFO     *GpeEventInfo;
    UINT32                  GpeNumber;
    ACPI_CPU_FLAGS          Flags;
    ACPI_STATUS             Status = AE_OK;
    UINT32                  GpeDispatchType;
    ACPI_GPE_HANDLER_INFO   *Handler;
    ACPI_GPE_NOTIFY_INFO    *List;
    ACPI_GPE_NOTIFY_INFO    *FreeList = NULL;


    ACPI_FUNCTION_TRACE_U32 (EvClearGpeDispatchByOwner, OwnerId);


    if (OwnerId == 0)
    {
        return_ACPI_STATUS (AE_OK);
    }

    Status = AcpiUtAcquireMutex (ACPI_MTX_EVENTS);
    if (ACPI_FAILURE (Status))
    {
        return_ACPI_STATUS (Status);
    }

    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);

    /* Walk all GPE interrupt blocks */

    GpeXruptInfo = AcpiGbl_GpeXruptListHead;
    while (GpeXruptInfo)
    {
        GpeBlock = GpeXruptInfo->GpeBlockListHead;
        while (GpeBlock)
        {
            /*
             * Walk all GPEs in this block to make sure none are blocked from
             * getting cleared with DisableForDispatch
             */

            for (GpeNumber = 0; GpeNumber < GpeBlock->GpeCount; GpeNumber++)
            {
                GpeEventInfo = &GpeBlock->EventInfo[GpeNumber];
                GpeDispatchType = AcpiEvDispatchTypeHiddenHandler(GpeEventInfo, OwnerId);
                if ((GpeDispatchType == ACPI_GPE_DISPATCH_METHOD ||
                     GpeDispatchType == ACPI_GPE_DISPATCH_NOTIFY) &&
                     GpeEventInfo->DisableForDispatch)
                {
                    /* At least one handler is polling or executing, cannot clear */
                    Status = AE_ABORT_METHOD;
                    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
                    (void) AcpiUtReleaseMutex (ACPI_MTX_EVENTS);
                    return_ACPI_STATUS (Status);
                }
            }

            GpeBlock = GpeBlock->Next;
        }

        GpeXruptInfo = GpeXruptInfo->Next;
    }

    GpeXruptInfo = AcpiGbl_GpeXruptListHead;
    while (GpeXruptInfo)
    {
        GpeBlock = GpeXruptInfo->GpeBlockListHead;
        while (GpeBlock)
        {
            /* Walk all GPEs in this block */

            for (GpeNumber = 0; GpeNumber < GpeBlock->GpeCount; GpeNumber++)
            {
                GpeEventInfo = &GpeBlock->EventInfo[GpeNumber];

                GpeDispatchType = AcpiEvDispatchTypeHiddenHandler(GpeEventInfo, OwnerId);
                switch(GpeDispatchType)
                {
                case ACPI_GPE_DISPATCH_METHOD:
                    /*
                     * GPE method belongs to table being unloaded. Clear
                     * only the dispatch type, preserve other flags.
                     */
                    GpeEventInfo->Flags &= ~ACPI_GPE_DISPATCH_MASK;
                    GpeEventInfo->Dispatch.MethodNode = NULL;

                    /*
                     * Hardware-disable the GPE so a late interrupt
                     * cannot fire and land in the "no handler" default
                     * branch with DisableForDispatch stuck at TRUE.
                     */
                    (void) AcpiHwLowSetGpe (
                        GpeEventInfo, ACPI_GPE_DISABLE);
                    break;

                case ACPI_GPE_DISPATCH_NOTIFY:
                    if (AcpiEvUnlinkNotifyListDevId(OwnerId, &GpeEventInfo->Dispatch.NotifyList, &FreeList))
                    {
                        GpeEventInfo->Flags &= ~ACPI_GPE_DISPATCH_MASK;
                        (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
                    }
                    break;

                case ACPI_GPE_DISPATCH_HANDLER:
                case ACPI_GPE_DISPATCH_RAW_HANDLER:

                    Handler = GpeEventInfo->Dispatch.Handler;
                    GpeDispatchType = ACPI_GPE_DISPATCH_TYPE (Handler->OriginalFlags);

                    /* Forget the hidden method so handler removal can't restore it */
                    if (GpeDispatchType == ACPI_GPE_DISPATCH_METHOD)
                    {
                        Handler->MethodNode = NULL;
                        Handler->OriginalFlags &= ~ACPI_GPE_DISPATCH_MASK; /* -> NONE */
                        Handler->OriginallyEnabled = FALSE;
                    }
                    else if (GpeDispatchType == ACPI_GPE_DISPATCH_NOTIFY)
                    {
                        List = ACPI_CAST_PTR(ACPI_GPE_NOTIFY_INFO, Handler->MethodNode);
                        if (AcpiEvUnlinkNotifyListDevId (OwnerId, &List, &FreeList))
                        {
                            Handler->OriginalFlags &= ~ACPI_GPE_DISPATCH_MASK;
                            Handler->OriginallyEnabled = FALSE;
                        }
                        Handler->MethodNode = ACPI_CAST_PTR (ACPI_NAMESPACE_NODE, List);
                    }
                    break;

                default:
                    break;
                }
            }

            GpeBlock = GpeBlock->Next;
        }

        GpeXruptInfo = GpeXruptInfo->Next;
    }

    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);

    /* Free the FreeList elements from the Notify path */
    while (FreeList)
    {
        List = FreeList->Next;
        ACPI_FREE (FreeList);
        FreeList = List;
    }

    (void) AcpiUtReleaseMutex (ACPI_MTX_EVENTS);
    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvAsynchEnableGpe
 *
 * PARAMETERS:  Context (GpeEventInfo) - Info for this GPE
 *              Callback from AcpiOsExecute
 *
 * RETURN:      None
 *
 * DESCRIPTION: Asynchronous clear/enable for GPE. This allows the GPE to
 *              complete (i.e., finish execution of Notify)
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
AcpiEvAsynchEnableGpe (
    void                    *Context)
{
    ACPI_GPE_EVENT_INFO     *GpeEventInfo = Context;
    ACPI_CPU_FLAGS          Flags;


    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    (void) AcpiEvFinishGpe (GpeEventInfo);
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);

    return;
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvFinishGpe
 *
 * PARAMETERS:  GpeEventInfo        - Info for this GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear/Enable a GPE. Common code that is used after execution
 *              of a GPE method or a synchronous or asynchronous GPE handler.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiEvFinishGpe (
    ACPI_GPE_EVENT_INFO     *GpeEventInfo)
{
    ACPI_STATUS             Status;


    if ((GpeEventInfo->Flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
            ACPI_GPE_LEVEL_TRIGGERED)
    {
        /*
         * GPE is level-triggered, we clear the GPE status bit after
         * handling the event.
         */
        Status = AcpiHwClearGpe (GpeEventInfo);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }
    }

    /*
     * Enable this GPE, conditionally. This means that the GPE will
     * only be physically enabled if the EnableMask bit is set
     * in the EventInfo.
     */
    (void) AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_CONDITIONAL_ENABLE);
    GpeEventInfo->DisableForDispatch = FALSE;
    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvDetectGpe
 *
 * PARAMETERS:  GpeDevice           - Device node. NULL for GPE0/GPE1
 *              GpeEventInfo        - Info for this GPE
 *              GpeNumber           - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect and dispatch a General Purpose Event to either a function
 *              (e.g. EC) or method (e.g. _Lxx/_Exx) handler.
 * NOTE:        GPE is W1C, so it is possible to handle a single GPE from both
 *              task and irq context in parallel as long as the process to
 *              detect and mask the GPE is atomic.
 *              However the atomicity of ACPI_GPE_DISPATCH_RAW_HANDLER is
 *              dependent on the raw handler itself.
 *
 ******************************************************************************/

UINT32
AcpiEvDetectGpe (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  GpeNumber)
{
    UINT32                  IntStatus = ACPI_INTERRUPT_NOT_HANDLED;
    UINT8                   EnabledStatusByte;
    UINT64                  StatusReg;
    UINT64                  EnableReg;
    UINT32                  RegisterBit;
    ACPI_GPE_REGISTER_INFO  *GpeRegisterInfo;
    ACPI_GPE_HANDLER_INFO   *GpeHandlerInfo;
    ACPI_CPU_FLAGS          Flags;
    ACPI_STATUS             Status;


    ACPI_FUNCTION_TRACE (EvGpeDetect);


    Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);

    if (!GpeEventInfo)
    {
        GpeEventInfo = AcpiEvGetGpeEventInfo (GpeDevice, GpeNumber);
        if (!GpeEventInfo)
        {
            goto ErrorExit;
        }
    }

    /* Get the info block for the entire GPE register */

    GpeRegisterInfo = GpeEventInfo->RegisterInfo;

    /* Get the register bitmask for this GPE */

    RegisterBit = AcpiHwGetGpeRegisterBit (GpeEventInfo);

    /* GPE currently enabled (enable bit == 1)? */

    Status = AcpiHwRead (&EnableReg, &GpeRegisterInfo->EnableAddress);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    /* GPE currently active (status bit == 1)? */

    Status = AcpiHwRead (&StatusReg, &GpeRegisterInfo->StatusAddress);
    if (ACPI_FAILURE (Status))
    {
        goto ErrorExit;
    }

    /* Check if there is anything active at all in this GPE */

    ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
        "Read registers for GPE %02X: Status=%02X, Enable=%02X, "
        "RunEnable=%02X, WakeEnable=%02X\n",
        GpeNumber,
        (UINT32) (StatusReg & RegisterBit),
        (UINT32) (EnableReg & RegisterBit),
        GpeRegisterInfo->EnableForRun,
        GpeRegisterInfo->EnableForWake));

    EnabledStatusByte = (UINT8) (StatusReg & EnableReg);
    if (!(EnabledStatusByte & RegisterBit))
    {
        goto ErrorExit;
    }

    /* Invoke global event handler if present */

    AcpiGpeCount++;
    if (AcpiGbl_GlobalEventHandler)
    {
        AcpiGbl_GlobalEventHandler (ACPI_EVENT_TYPE_GPE,
            GpeDevice, GpeNumber,
            AcpiGbl_GlobalEventHandlerContext);
    }

    /* Found an active GPE */

    if (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags) ==
        ACPI_GPE_DISPATCH_RAW_HANDLER)
    {
        /* Dispatch the event to a raw handler */

        GpeHandlerInfo = GpeEventInfo->Dispatch.Handler;

        /*
         * There is no protection around the namespace node
         * and the GPE handler to ensure a safe destruction
         * because:
         * 1. The namespace node is expected to always
         *    exist after loading a table.
         * 2. The GPE handler is expected to be flushed by
         *    AcpiOsWaitEventsComplete() before the
         *    destruction.
         */
        AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
        IntStatus |= GpeHandlerInfo->Address (
            GpeDevice, GpeNumber, GpeHandlerInfo->Context);
        Flags = AcpiOsAcquireLock (AcpiGbl_GpeLock);
    }
    else
    {
        /* Dispatch the event to a standard handler or method. */

        IntStatus |= AcpiEvGpeDispatch (GpeDevice,
            GpeEventInfo, GpeNumber);
    }

ErrorExit:
    AcpiOsReleaseLock (AcpiGbl_GpeLock, Flags);
    return (IntStatus);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiEvGpeDispatch
 *
 * PARAMETERS:  GpeDevice           - Device node. NULL for GPE0/GPE1
 *              GpeEventInfo        - Info for this GPE
 *              GpeNumber           - Number relative to the parent GPE block
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Dispatch a General Purpose Event to either a function (e.g. EC)
 *              or method (e.g. _Lxx/_Exx) handler.
 *
 ******************************************************************************/

UINT32
AcpiEvGpeDispatch (
    ACPI_NAMESPACE_NODE     *GpeDevice,
    ACPI_GPE_EVENT_INFO     *GpeEventInfo,
    UINT32                  GpeNumber)
{
    ACPI_STATUS             Status;
    UINT32                  ReturnValue;


    ACPI_FUNCTION_TRACE (EvGpeDispatch);


    /*
     * Always disable the GPE so that it does not keep firing before
     * any asynchronous activity completes (either from the execution
     * of a GPE method or an asynchronous GPE handler.)
     *
     * If there is no handler or method to run, just disable the
     * GPE and leave it disabled permanently to prevent further such
     * pointless events from firing.
     */
    Status = AcpiHwLowSetGpe (GpeEventInfo, ACPI_GPE_DISABLE);
    if (ACPI_FAILURE (Status))
    {
        ACPI_EXCEPTION ((AE_INFO, Status,
            "Unable to disable GPE %02X", GpeNumber));
        return_UINT32 (ACPI_INTERRUPT_NOT_HANDLED);
    }

    /*
     * If edge-triggered, clear the GPE status bit now. Note that
     * level-triggered events are cleared after the GPE is serviced.
     */
    if ((GpeEventInfo->Flags & ACPI_GPE_XRUPT_TYPE_MASK) ==
            ACPI_GPE_EDGE_TRIGGERED)
    {
        Status = AcpiHwClearGpe (GpeEventInfo);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Unable to clear GPE %02X", GpeNumber));
            (void) AcpiHwLowSetGpe (
                GpeEventInfo, ACPI_GPE_CONDITIONAL_ENABLE);
            return_UINT32 (ACPI_INTERRUPT_NOT_HANDLED);
        }
    }

    GpeEventInfo->DisableForDispatch = TRUE;

    /*
     * Dispatch the GPE to either an installed handler or the control
     * method associated with this GPE (_Lxx or _Exx). If a handler
     * exists, we invoke it and do not attempt to run the method.
     * If there is neither a handler nor a method, leave the GPE
     * disabled.
     */
    switch (ACPI_GPE_DISPATCH_TYPE (GpeEventInfo->Flags))
    {
    case ACPI_GPE_DISPATCH_HANDLER:

        /* Invoke the installed handler (at interrupt level) */

        ReturnValue = GpeEventInfo->Dispatch.Handler->Address (
            GpeDevice, GpeNumber,
            GpeEventInfo->Dispatch.Handler->Context);

        /* If requested, clear (if level-triggered) and re-enable the GPE */

        if (ReturnValue & ACPI_REENABLE_GPE)
        {
            (void) AcpiEvFinishGpe (GpeEventInfo);
        }
        break;

    case ACPI_GPE_DISPATCH_METHOD:
    case ACPI_GPE_DISPATCH_NOTIFY:
        /*
         * Execute the method associated with the GPE
         * NOTE: Level-triggered GPEs are cleared after the method completes.
         */
        Status = AcpiOsExecute (OSL_GPE_HANDLER,
            AcpiEvAsynchExecuteGpeMethod, GpeEventInfo);
        if (ACPI_FAILURE (Status))
        {
            ACPI_EXCEPTION ((AE_INFO, Status,
                "Unable to queue handler for GPE %02X - event disabled",
                GpeNumber));
        }
        break;

    default:
        /*
         * No handler or method to run!
         * 03/2010: This case should no longer be possible. We will not allow
         * a GPE to be enabled if it has no handler or method.
         */
        ACPI_ERROR ((AE_INFO,
            "No handler or method for GPE %02X, disabling event",
            GpeNumber));
        break;
    }

    return_UINT32 (ACPI_INTERRUPT_HANDLED);
}

#endif /* !ACPI_REDUCED_HARDWARE */
