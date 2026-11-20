    /*
     * Some or all of this work - Copyright (c) 2006 - 2021, Intel Corp.
     * All rights reserved.
     *
     * Redistribution and use in source and binary forms, with or without modification,
     * are permitted provided that the following conditions are met:
     *
     * Redistributions of source code must retain the above copyright notice,
     * this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright notice,
     * this list of conditions and the following disclaimer in the documentation
     * and/or other materials provided with the distribution.
     * Neither the name of Intel Corporation nor the names of its contributors
     * may be used to endorse or promote products derived from this software
     * without specific prior written permission.
     *
     * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
     * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
     * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
     * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
     * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
     * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
     * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
     * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
     * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
     * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
     */
    /*
     * Bug 0076:
     *
     * SUMMARY: Unexpected dereference of Index reference in Store operator
     *
     * Store (Index (...), LocalX) must leave the Index reference in LocalX,
     * not a copy of the referenced element. Per the ACPI specification
     * ("LocalX Objects"), reading LocalX that holds an ObjectReference
     * returns the reference and only DerefOf returns the target, so:
     *
     *  - DerefOf (LocalX) must succeed and yield the element; it would raise
     *    AE_TYPE if LocalX held a plain value instead of the reference,
     *  - after the package element is modified, DerefOf (LocalX) must yield
     *    the new value, which proves that LocalX refers to the element
     *    itself and not to a copy.
     *
     * Formerly the check expected AE_AML_OPERAND_TYPE from Add (LocalX, 0).
     * The interpreter now resolves an Index reference held in LocalX when
     * it is used as an integer operand, so that exception no longer occurs
     * and can no longer serve as a proof that the reference was preserved.
     */
    Method (ME29, 0, NotSerialized)
    {
        Local0 = PD01 [0x00]
        Debug = Local0
        Store (PD01 [0x00], Local1)
        Debug = Local1
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        /* Both Locals must hold a reference: DerefOf fails on a plain value */

        Local2 = DerefOf (Local0)
        Local3 = DerefOf (Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        If ((Local2 != 0x89ABCDEF))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local2, 0x89ABCDEF)
        }

        If ((Local3 != 0x89ABCDEF))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local3, 0x89ABCDEF)
        }

        /* The references must point at the element itself, not at a copy */

        PD01 [0x00] = 0x12345678
        Local2 = DerefOf (Local0)
        Local3 = DerefOf (Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        If ((Local2 != 0x12345678))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local2, 0x12345678)
        }

        If ((Local3 != 0x12345678))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local3, 0x12345678)
        }

        /* Restore the shared data object */

        PD01 [0x00] = 0x89ABCDEF
        Local2 = RefOf (ID0A)
        Debug = Local2
    }
