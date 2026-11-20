/******************************************************************************
 *
 * Module Name: psutils - Parser miscellaneous utilities (Parser only)
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
#include "acparser.h"
#include "amlcode.h"
#include "acconvert.h"

#define _COMPONENT          ACPI_PARSER
        ACPI_MODULE_NAME    ("psutils")


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsCreateScopeOp
 *
 * PARAMETERS:  None
 *
 * RETURN:      A new Scope object, null on failure
 *
 * DESCRIPTION: Create a Scope and associated namepath op with the root name
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT *
AcpiPsCreateScopeOp (
    UINT8                   *Aml)
{
    ACPI_PARSE_OBJECT       *ScopeOp;


    ScopeOp = AcpiPsAllocOp (AML_SCOPE_OP, Aml);
    if (!ScopeOp)
    {
        return (NULL);
    }

    ScopeOp->Named.Name = ACPI_ROOT_NAME;
    return (ScopeOp);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsInitOp
 *
 * PARAMETERS:  Op              - A newly allocated Op object
 *              Opcode          - Opcode to store in the Op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Initialize a parse (Op) object
 *
 ******************************************************************************/

void
AcpiPsInitOp (
    ACPI_PARSE_OBJECT       *Op,
    UINT16                  Opcode)
{
    ACPI_FUNCTION_ENTRY ();


    Op->Common.DescriptorType = ACPI_DESC_TYPE_PARSER;
    Op->Common.AmlOpcode = Opcode;

    /*
     * The opcode name is a pointer into the static AcpiGbl_AmlOpInfo table and
     * lives for the life of the program, so the Op only has to remember where
     * it is. Copying it cost a 16-byte strncpy on every parse op.
     */
    ACPI_DISASM_ONLY_MEMBERS (Op->Common.AmlOpName =
        (AcpiPsGetOpcodeInfo (Opcode))->Name);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsAllocOp
 *
 * PARAMETERS:  Opcode          - Opcode that will be stored in the new Op
 *              Aml             - Address of the opcode
 *
 * RETURN:      Pointer to the new Op, null on failure
 *
 * DESCRIPTION: Allocate an acpi_op, choose op type (and thus size) based on
 *              opcode. A cache of opcodes is available for the pure
 *              GENERIC_OP, since this is by far the most commonly used.
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT*
AcpiPsAllocOp (
    UINT16                  Opcode,
    UINT8                   *Aml)
{
    ACPI_PARSE_OBJECT       *Op;
    const ACPI_OPCODE_INFO  *OpInfo;
    UINT8                   Flags = ACPI_PARSEOP_GENERIC;


    ACPI_FUNCTION_ENTRY ();


    OpInfo = AcpiPsGetOpcodeInfo (Opcode);

    /* Determine type of ParseOp required */

    if (OpInfo->Flags & AML_DEFER)
    {
        Flags = ACPI_PARSEOP_DEFERRED;
    }
    else if (OpInfo->Flags & AML_NAMED)
    {
        Flags = ACPI_PARSEOP_NAMED_OBJECT;
    }
    else if (Opcode == AML_INT_BYTELIST_OP)
    {
        Flags = ACPI_PARSEOP_BYTELIST;
    }

    /* Allocate the minimum required size object */

    if (Flags == ACPI_PARSEOP_GENERIC)
    {
        /* The generic op (default) is by far the most common (16 to 1) */

        Op = AcpiOsAcquireObject (AcpiGbl_PsNodeCache);
    }
    else
    {
        /* Extended parseop */

        Op = AcpiOsAcquireObject (AcpiGbl_PsNodeExtCache);
    }

    /* Initialize the Op */

    if (Op)
    {
        AcpiPsInitOp (Op, Opcode);
        Op->Common.Aml = Aml;
        Op->Common.Flags = Flags;
        ASL_CV_CLEAR_OP_COMMENTS(Op);

        if (Opcode == AML_SCOPE_OP)
        {
            AcpiGbl_CurrentScope = Op;
        }

        if (AcpiGbl_CaptureComments)
        {
            ASL_CV_TRANSFER_COMMENTS (Op);
        }
    }

    return (Op);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiPsFreeOp
 *
 * PARAMETERS:  Op              - Op to be freed
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an Op object. Either put it on the GENERIC_OP cache list
 *              or actually free it.
 *
 ******************************************************************************/

void
AcpiPsFreeOp (
    ACPI_PARSE_OBJECT       *Op)
{
    ACPI_FUNCTION_NAME (PsFreeOp);


    ASL_CV_CLEAR_OP_COMMENTS(Op);
    if (Op->Common.AmlOpcode == AML_INT_RETURN_VALUE_OP)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS,
            "Free retval op: %p\n", Op));
    }

    if (Op->Common.Flags & ACPI_PARSEOP_GENERIC)
    {
        (void) AcpiOsReleaseObject (AcpiGbl_PsNodeCache, Op);
    }
    else
    {
        (void) AcpiOsReleaseObject (AcpiGbl_PsNodeExtCache, Op);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    Utility functions
 *
 * DESCRIPTION: Low level character and object functions
 *
 ******************************************************************************/


/*
 * Is "c" a namestring lead character?
 */
BOOLEAN
AcpiPsIsLeadingChar (
    UINT32                  c)
{
    return ((BOOLEAN) (c == '_' || (c >= 'A' && c <= 'Z')));
}


/*
 * Get op's name (4-byte name segment) or 0 if unnamed
 */
UINT32
AcpiPsGetName (
    ACPI_PARSE_OBJECT       *Op)
{

    /* The "generic" object has no name associated with it */

    if (Op->Common.Flags & ACPI_PARSEOP_GENERIC)
    {
        return (0);
    }

    /* Only the "Extended" parse objects have a name */

    return (Op->Named.Name);
}


/*
 * Set op's name
 */
void
AcpiPsSetName (
    ACPI_PARSE_OBJECT       *Op,
    UINT32                  name)
{

    /* The "generic" object has no name associated with it */

    if (Op->Common.Flags & ACPI_PARSEOP_GENERIC)
    {
        return;
    }

    Op->Named.Name = name;
}
