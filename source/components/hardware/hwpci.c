/*******************************************************************************
 *
 * Module Name: hwpci - Obtain PCI bus, device, and function numbers
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Copyright (c) 1999 - 2026, Intel Corp.
 * SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only
 *
 *****************************************************************************/

#include "acpi.h"
#include "accommon.h"


#define _COMPONENT          ACPI_NAMESPACE
        ACPI_MODULE_NAME    ("hwpci")


/* PCI configuration space values */

#define PCI_CFG_HEADER_TYPE_REG             0x0E
#define PCI_CFG_SECONDARY_BUS_NUMBER_REG    0x19

/* PCI header values */

#define PCI_HEADER_TYPE_MASK                0x7F
#define PCI_TYPE_BRIDGE                     0x01
#define PCI_TYPE_CARDBUS_BRIDGE             0x02

typedef struct acpi_pci_device
{
    ACPI_HANDLE             Device;
    struct acpi_pci_device  *Next;

} ACPI_PCI_DEVICE;


/* Local prototypes */

static ACPI_STATUS
AcpiHwBuildPciList (
    ACPI_HANDLE             RootPciDevice,
    ACPI_HANDLE             PciRegion,
    ACPI_PCI_DEVICE         **ReturnListHead);

static ACPI_STATUS
AcpiHwProcessPciList (
    ACPI_PCI_ID             *PciId,
    ACPI_PCI_DEVICE         *ListHead);

static void
AcpiHwDeletePciList (
    ACPI_PCI_DEVICE         *ListHead);

static ACPI_STATUS
AcpiHwGetPciDeviceInfo (
    ACPI_PCI_ID             *PciId,
    ACPI_HANDLE             PciDevice,
    UINT16                  *BusNumber,
    BOOLEAN                 *IsBridge);


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwDerivePciId
 *
 * PARAMETERS:  PciId               - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              RootPciDevice       - A handle to a PCI device object. This
 *                                    object must be a PCI Root Bridge having a
 *                                    _HID value of either PNP0A03 or PNP0A08
 *              PciRegion           - A handle to a PCI configuration space
 *                                    Operation Region being initialized
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function derives a full PCI ID for a PCI device,
 *              consisting of a Segment number, Bus number, Device number,
 *              and function code.
 *
 *              The PCI hardware dynamically configures PCI bus numbers
 *              depending on the bus topology discovered during system
 *              initialization. This function is invoked during configuration
 *              of a PCI_Config Operation Region in order to (possibly) update
 *              the Bus/Device/Function numbers in the PciId with the actual
 *              values as determined by the hardware and operating system
 *              configuration.
 *
 *              The PciId parameter is initially populated during the Operation
 *              Region initialization. This function is then called, and is
 *              will make any necessary modifications to the Bus, Device, or
 *              Function number PCI ID subfields as appropriate for the
 *              current hardware and OS configuration.
 *
 * NOTE:        Created 08/2010. Replaces the previous OSL AcpiOsDerivePciId
 *              interface since this feature is OS-independent. This module
 *              specifically avoids any use of recursion by building a local
 *              temporary device list.
 *
 ******************************************************************************/

ACPI_STATUS
AcpiHwDerivePciId (
    ACPI_PCI_ID             *PciId,
    ACPI_HANDLE             RootPciDevice,
    ACPI_HANDLE             PciRegion)
{
    ACPI_STATUS             Status;
    ACPI_PCI_DEVICE         *ListHead;


    ACPI_FUNCTION_TRACE (HwDerivePciId);


    if (!PciId)
    {
        return_ACPI_STATUS (AE_BAD_PARAMETER);
    }

    /* Build a list of PCI devices, from PciRegion up to RootPciDevice */

    Status = AcpiHwBuildPciList (RootPciDevice, PciRegion, &ListHead);
    if (ACPI_SUCCESS (Status))
    {
        /* Walk the list, updating the PCI device/function/bus numbers */

        Status = AcpiHwProcessPciList (PciId, ListHead);

        /* Delete the list */

        AcpiHwDeletePciList (ListHead);
    }

    return_ACPI_STATUS (Status);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwBuildPciList
 *
 * PARAMETERS:  RootPciDevice       - A handle to a PCI device object. This
 *                                    object is guaranteed to be a PCI Root
 *                                    Bridge having a _HID value of either
 *                                    PNP0A03 or PNP0A08
 *              PciRegion           - A handle to the PCI configuration space
 *                                    Operation Region
 *              ReturnListHead      - Where the PCI device list is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Builds a list of devices from the input PCI region up to the
 *              Root PCI device for this namespace subtree.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwBuildPciList (
    ACPI_HANDLE             RootPciDevice,
    ACPI_HANDLE             PciRegion,
    ACPI_PCI_DEVICE         **ReturnListHead)
{
    ACPI_HANDLE             CurrentDevice;
    ACPI_HANDLE             ParentDevice;
    ACPI_STATUS             Status;
    ACPI_PCI_DEVICE         *ListElement;


    /*
     * Ascend namespace branch until the RootPciDevice is reached, building
     * a list of device nodes. Loop will exit when either the PCI device is
     * found, or the root of the namespace is reached.
     */
    *ReturnListHead = NULL;
    CurrentDevice = PciRegion;
    while (1)
    {
        Status = AcpiGetParent (CurrentDevice, &ParentDevice);
        if (ACPI_FAILURE (Status))
        {
            /* Must delete the list before exit */

            AcpiHwDeletePciList (*ReturnListHead);
            return (Status);
        }

        /* Finished when we reach the PCI root device (PNP0A03 or PNP0A08) */

        if (ParentDevice == RootPciDevice)
        {
            return (AE_OK);
        }

        ListElement = ACPI_ALLOCATE (sizeof (ACPI_PCI_DEVICE));
        if (!ListElement)
        {
            /* Must delete the list before exit */

            AcpiHwDeletePciList (*ReturnListHead);
            return (AE_NO_MEMORY);
        }

        /* Put new element at the head of the list */

        ListElement->Next = *ReturnListHead;
        ListElement->Device = ParentDevice;
        *ReturnListHead = ListElement;

        CurrentDevice = ParentDevice;
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwProcessPciList
 *
 * PARAMETERS:  PciId               - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              ListHead            - Device list created by
 *                                    AcpiHwBuildPciList
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk downward through the PCI device list, getting the device
 *              info for each, via the PCI configuration space and updating
 *              the PCI ID as necessary. Deletes the list during traversal.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwProcessPciList (
    ACPI_PCI_ID             *PciId,
    ACPI_PCI_DEVICE         *ListHead)
{
    ACPI_STATUS             Status = AE_OK;
    ACPI_PCI_DEVICE         *Info;
    UINT16                  BusNumber;
    BOOLEAN                 IsBridge = TRUE;


    ACPI_FUNCTION_NAME (HwProcessPciList);


    ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
        "Input PciId:  Seg %4.4X Bus %4.4X Dev %4.4X Func %4.4X\n",
        PciId->Segment, PciId->Bus, PciId->Device, PciId->Function));

    BusNumber = PciId->Bus;

    /*
     * Descend down the namespace tree, collecting PCI device, function,
     * and bus numbers. BusNumber is only important for PCI bridges.
     * Algorithm: As we descend the tree, use the last valid PCI device,
     * function, and bus numbers that are discovered, and assign them
     * to the PCI ID for the target device.
     */
    Info = ListHead;
    while (Info)
    {
        Status = AcpiHwGetPciDeviceInfo (PciId, Info->Device,
            &BusNumber, &IsBridge);
        if (ACPI_FAILURE (Status))
        {
            return (Status);
        }

        Info = Info->Next;
    }

    ACPI_DEBUG_PRINT ((ACPI_DB_OPREGION,
        "Output PciId: Seg %4.4X Bus %4.4X Dev %4.4X Func %4.4X "
        "Status %X BusNumber %X IsBridge %X\n",
        PciId->Segment, PciId->Bus, PciId->Device, PciId->Function,
        Status, BusNumber, IsBridge));

    return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwDeletePciList
 *
 * PARAMETERS:  ListHead            - Device list created by
 *                                    AcpiHwBuildPciList
 *
 * RETURN:      None
 *
 * DESCRIPTION: Free the entire PCI list.
 *
 ******************************************************************************/

static void
AcpiHwDeletePciList (
    ACPI_PCI_DEVICE         *ListHead)
{
    ACPI_PCI_DEVICE         *Next;
    ACPI_PCI_DEVICE         *Previous;


    Next = ListHead;
    while (Next)
    {
        Previous = Next;
        Next = Previous->Next;
        ACPI_FREE (Previous);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiHwGetPciDeviceInfo
 *
 * PARAMETERS:  PciId               - Initial values for the PCI ID. May be
 *                                    modified by this function.
 *              PciDevice           - Handle for the PCI device object
 *              BusNumber           - Where a PCI bridge bus number is returned
 *              IsBridge            - Return value, indicates if this PCI
 *                                    device is a PCI bridge
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the device info for a single PCI device object. Get the
 *              _ADR (contains PCI device and function numbers), and for PCI
 *              bridge devices, get the bus number from PCI configuration
 *              space.
 *
 ******************************************************************************/

static ACPI_STATUS
AcpiHwGetPciDeviceInfo (
    ACPI_PCI_ID             *PciId,
    ACPI_HANDLE             PciDevice,
    UINT16                  *BusNumber,
    BOOLEAN                 *IsBridge)
{
    ACPI_STATUS             Status;
    ACPI_OBJECT_TYPE        ObjectType;
    UINT64                  ReturnValue;
    UINT64                  PciValue;


    /* We only care about objects of type Device */

    Status = AcpiGetType (PciDevice, &ObjectType);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    if (ObjectType != ACPI_TYPE_DEVICE)
    {
        return (AE_OK);
    }

    /* We need an _ADR. Ignore device if not present */

    Status = AcpiUtEvaluateNumericObject (METHOD_NAME__ADR,
        PciDevice, &ReturnValue);
    if (ACPI_FAILURE (Status))
    {
        return (AE_OK);
    }

    /*
     * From _ADR, get the PCI Device and Function and
     * update the PCI ID.
     */
    PciId->Device = ACPI_HIWORD (ACPI_LODWORD (ReturnValue));
    PciId->Function = ACPI_LOWORD (ACPI_LODWORD (ReturnValue));

    /*
     * If the previous device was a bridge, use the previous
     * device bus number
     */
    if (*IsBridge)
    {
        PciId->Bus = *BusNumber;
    }

    /*
     * Get the bus numbers from PCI Config space:
     *
     * First, get the PCI HeaderType
     */
    *IsBridge = FALSE;
    Status = AcpiOsReadPciConfiguration (PciId,
        PCI_CFG_HEADER_TYPE_REG, &PciValue, 8);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* We only care about bridges (1=PciBridge, 2=CardBusBridge) */

    PciValue &= PCI_HEADER_TYPE_MASK;

    if ((PciValue != PCI_TYPE_BRIDGE) &&
        (PciValue != PCI_TYPE_CARDBUS_BRIDGE))
    {
        return (AE_OK);
    }

    /*
     * Bridge: The bus number of the bridge itself is already known, either
     * from the secondary bus number of the parent bridge or from the _BBN
     * of the root bridge. Do not replace it with the primary bus number
     * register of the bridge: that register reads as zero after the bridge
     * has been powered up (e.g. from D3cold, while its _PS0 is executing)
     * and before the OS has restored its configuration space. The derived
     * PCI ID would then refer to bus 0, and since it is cached for the
     * lifetime of the operation region, all later accesses would go to
     * the wrong device.
     */
    *IsBridge = TRUE;

    /* Bridge: Get the Secondary BusNumber */

    Status = AcpiOsReadPciConfiguration (PciId,
        PCI_CFG_SECONDARY_BUS_NUMBER_REG, &PciValue, 8);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    *BusNumber = (UINT16) PciValue;
    return (AE_OK);
}
