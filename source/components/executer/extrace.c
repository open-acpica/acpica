/******************************************************************************
 *
 * Module Name: extrace - Support for interpreter execution tracing
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
#include "acnamesp.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_EXECUTER
        ACPI_MODULE_NAME    ("extrace")


static ACPI_OPERAND_OBJECT  *AcpiGbl_TraceMethodObject = NULL;

/* Local prototypes */

#ifdef ACPI_DEBUG_OUTPUT
static const char *
AcpiExGetTraceEventName (
    ACPI_TRACE_EVENT_TYPE   Type);
#endif


/*******************************************************************************
 *
 * FUNCTION:    AcpiExInterpreterTraceEnabled
 *
 * PARAMETERS:  Name                - Whether method name should be matched,
 *                                    this should be checked before starting
 *                                    the tracer
 *
 * RETURN:      TRUE if interpreter trace is enabled.
 *
 * DESCRIPTION: Check whether interpreter trace is enabled
 *
 ******************************************************************************/

static BOOLEAN
AcpiExInterpreterTraceEnabled (
    char                    *Name)
{

    /* Check if tracing is enabled */

    if (!(AcpiGbl_TraceFlags & ACPI_TRACE_ENABLED))
    {
        return (FALSE);
    }

    /*
     * Check if tracing is filtered:
     *
     * 1. If the tracer is started, AcpiGbl_TraceMethodObject should have
     *    been filled by the trace starter
     * 2. If the tracer is not started, AcpiGbl_TraceMethodName should be
     *    matched if it is specified
     * 3. If the tracer is oneshot style, AcpiGbl_TraceMethodName should
     *    not be cleared by the trace stopper during the first match
     */
    if (AcpiGbl_TraceMethodObject)
    {
        return (TRUE);
    }

    if (Name &&
        (AcpiGbl_TraceMethodName &&
         strcmp (AcpiGbl_TraceMethodName, Name)))
    {
        return (FALSE);
    }

    if ((AcpiGbl_TraceFlags & ACPI_TRACE_ONESHOT) &&
        !AcpiGbl_TraceMethodName)
    {
        return (FALSE);
    }

    return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExGetTraceEventName
 *
 * PARAMETERS:  Type            - Trace event type
 *
 * RETURN:      Trace event name.
 *
 * DESCRIPTION: Used to obtain the full trace event name.
 *
 ******************************************************************************/

#ifdef ACPI_DEBUG_OUTPUT

static const char *
AcpiExGetTraceEventName (
    ACPI_TRACE_EVENT_TYPE   Type)
{

    switch (Type)
    {
    case ACPI_TRACE_AML_METHOD:

        return "Method";

    case ACPI_TRACE_AML_OPCODE:

        return "Opcode";

    case ACPI_TRACE_AML_REGION:

        return "Region";

    default:

        return "";
    }
}

#endif

/*******************************************************************************
 *
 * FUNCTION:    AcpiExTraceArgs
 *
 * PARAMETERS:  Params            - AML method arguments
 *              Count             - number of method arguments
 *
 * RETURN:      None
 *
 * DESCRIPTION: Trace any arguments
 *
 ******************************************************************************/

void
AcpiExTraceArgs(ACPI_OPERAND_OBJECT **Params, UINT32 Count)
{
    UINT32 i;

    ACPI_FUNCTION_NAME(ExTraceArgs);

    for (i = 0; i < Count; i++)
    {
        ACPI_OPERAND_OBJECT *obj_desc = Params[i];

        if (!i)
        {
            ACPI_DEBUG_PRINT((ACPI_DB_TRACE_POINT, " "));
        }

        switch (obj_desc->Common.Type)
        {
        case ACPI_TYPE_INTEGER:
            ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "%lx", obj_desc->Integer.Value));
            break;

        case ACPI_TYPE_STRING:
            if (!obj_desc->String.Length)
            {
                ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "NULL"));
                break;
            }
            if (ACPI_IS_DEBUG_ENABLED(ACPI_LV_TRACE_POINT, _COMPONENT))
            {
                AcpiUtPrintString(obj_desc->String.Pointer, ACPI_UINT8_MAX);
            }
            break;

        default:
            ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "Unknown"));
            break;
        }

        if ((i + 1) == Count)
        {
            ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, "\n"));
        }
        else
        {
            ACPI_DEBUG_PRINT_RAW((ACPI_DB_TRACE_POINT, ", "));
        }
    }
}

/*******************************************************************************
 *
 * FUNCTION:    AcpiExTracePoint
 *
 * PARAMETERS:  Type                - Trace event type
 *              Begin               - TRUE if before execution
 *              Aml                 - Executed AML address
 *              Pathname            - Object path
 *
 * RETURN:      None
 *
 * DESCRIPTION: Internal interpreter execution trace.
 *
 ******************************************************************************/

void
AcpiExTracePoint (
    ACPI_TRACE_EVENT_TYPE   Type,
    BOOLEAN                 Begin,
    UINT8                   *Aml,
    char                    *Pathname)
{

    ACPI_FUNCTION_NAME (ExTracePoint);


    if (Pathname)
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_TRACE_POINT,
            "%s %s [%s] execution.\n",
            AcpiExGetTraceEventName (Type), Begin ? "Begin" : "End",
            Pathname));
    }
    else
    {
        ACPI_DEBUG_PRINT ((ACPI_DB_TRACE_POINT,
            "%s %s [0x%p] execution.\n",
            AcpiExGetTraceEventName (Type), Begin ? "Begin" : "End",
            Aml));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStartTraceMethod
 *
 * PARAMETERS:  MethodNode          - Node of the method
 *              ObjDesc             - The method object
 *              WalkState           - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Start control method execution trace
 *
 ******************************************************************************/

void
AcpiExStartTraceMethod (
    ACPI_NAMESPACE_NODE     *MethodNode,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState)
{
    char                    *Pathname = NULL;
    BOOLEAN                 Enabled = FALSE;


    ACPI_FUNCTION_NAME (ExStartTraceMethod);


    if (MethodNode)
    {
        Pathname = AcpiNsGetNormalizedPathname (MethodNode, TRUE);
    }

    Enabled = AcpiExInterpreterTraceEnabled (Pathname);
    if (Enabled && !AcpiGbl_TraceMethodObject)
    {
        AcpiGbl_TraceMethodObject = ObjDesc;
        AcpiGbl_OriginalDbgLevel = AcpiDbgLevel;
        AcpiGbl_OriginalDbgLayer = AcpiDbgLayer;
        AcpiDbgLevel = ACPI_TRACE_LEVEL_ALL;
        AcpiDbgLayer = ACPI_TRACE_LAYER_ALL;

        if (AcpiGbl_TraceDbgLevel)
        {
            AcpiDbgLevel = AcpiGbl_TraceDbgLevel;
        }

        if (AcpiGbl_TraceDbgLayer)
        {
            AcpiDbgLayer = AcpiGbl_TraceDbgLayer;
        }
    }

    if (Enabled)
    {
        ACPI_TRACE_POINT (ACPI_TRACE_AML_METHOD, TRUE,
            ObjDesc ? ObjDesc->Method.AmlStart : NULL, Pathname);
    }

    if (Pathname)
    {
        ACPI_FREE (Pathname);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStopTraceMethod
 *
 * PARAMETERS:  MethodNode          - Node of the method
 *              ObjDesc             - The method object
 *              WalkState           - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop control method execution trace
 *
 ******************************************************************************/

void
AcpiExStopTraceMethod (
    ACPI_NAMESPACE_NODE     *MethodNode,
    ACPI_OPERAND_OBJECT     *ObjDesc,
    ACPI_WALK_STATE         *WalkState)
{
    char                    *Pathname = NULL;
    BOOLEAN                 Enabled;


    ACPI_FUNCTION_NAME (ExStopTraceMethod);


    if (MethodNode)
    {
        Pathname = AcpiNsGetNormalizedPathname (MethodNode, TRUE);
    }

    Enabled = AcpiExInterpreterTraceEnabled (NULL);

    if (Enabled)
    {
        ACPI_TRACE_POINT (ACPI_TRACE_AML_METHOD, FALSE,
            ObjDesc ? ObjDesc->Method.AmlStart : NULL, Pathname);
    }

    /* Check whether the tracer should be stopped */

    if (AcpiGbl_TraceMethodObject == ObjDesc)
    {
        /* Disable further tracing if type is one-shot */

        if (AcpiGbl_TraceFlags & ACPI_TRACE_ONESHOT)
        {
            AcpiGbl_TraceMethodName = NULL;
        }

        AcpiDbgLevel = AcpiGbl_OriginalDbgLevel;
        AcpiDbgLayer = AcpiGbl_OriginalDbgLayer;
        AcpiGbl_TraceMethodObject = NULL;
    }

    if (Pathname)
    {
        ACPI_FREE (Pathname);
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStartTraceOpcode
 *
 * PARAMETERS:  Op                  - The parser opcode object
 *              WalkState           - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Start opcode execution trace
 *
 ******************************************************************************/

void
AcpiExStartTraceOpcode (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState)
{

    ACPI_FUNCTION_NAME (ExStartTraceOpcode);


    if (AcpiExInterpreterTraceEnabled (NULL) &&
        (AcpiGbl_TraceFlags & ACPI_TRACE_OPCODE))
    {
        ACPI_TRACE_POINT (ACPI_TRACE_AML_OPCODE, TRUE,
            Op->Common.Aml, ACPI_CAST_PTR (char, Op->Common.AmlOpName));
    }
}


/*******************************************************************************
 *
 * FUNCTION:    AcpiExStopTraceOpcode
 *
 * PARAMETERS:  Op                  - The parser opcode object
 *              WalkState           - current state, NULL if not yet executing
 *                                    a method.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Stop opcode execution trace
 *
 ******************************************************************************/

void
AcpiExStopTraceOpcode (
    ACPI_PARSE_OBJECT       *Op,
    ACPI_WALK_STATE         *WalkState)
{

    ACPI_FUNCTION_NAME (ExStopTraceOpcode);


    if (AcpiExInterpreterTraceEnabled (NULL) &&
        (AcpiGbl_TraceFlags & ACPI_TRACE_OPCODE))
    {
        ACPI_TRACE_POINT (ACPI_TRACE_AML_OPCODE, FALSE,
            Op->Common.Aml, ACPI_CAST_PTR (char, Op->Common.AmlOpName));
    }
}
