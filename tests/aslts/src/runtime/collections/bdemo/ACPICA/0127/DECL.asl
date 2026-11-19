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
     * Bug 127:
     *
     * SUMMARY: Unexpectedly CopyObject of immediately passed Index reference is not reference
     *
     * CopyObject (Index (...), LocalX) and the chained Store
     * LocalY = LocalX = Index (...) must leave the Index reference in the
     * Locals, not a copy of the referenced element. Per the ACPI
     * specification ("LocalX Objects"), reading LocalX that holds an
     * ObjectReference returns the reference and only DerefOf returns the
     * target, so:
     *
     *  - for the initialized element PD11 [0], DerefOf (LocalX) must yield
     *    the element and, after the element is modified, the new value,
     *    which proves that LocalX refers to the element itself,
     *  - for the uninitialized element PD11 [1], DerefOf (LocalX) must
     *    raise AE_AML_UNINITIALIZED_ELEMENT ("DerefOf": dereferencing an
     *    Index reference to an uninitialized package element is a fatal
     *    error); only a reference to that element can produce it. If the
     *    copy dereferenced the Index, the exception would already occur
     *    in the Store or CopyObject itself.
     *
     * Formerly the check expected AE_AML_OPERAND_TYPE from Add (LocalX, 0).
     * The interpreter now resolves an Index reference held in LocalX when
     * it is used as an integer operand, so that exception no longer occurs
     * and can no longer serve as a proof that the reference was preserved.
     */
    /* Store */
    Method (MF13, 0, NotSerialized)
    {
        Local1 = Local0 = PD11 [0x00]
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        Local2 = DerefOf (Local0)
        Local3 = DerefOf (Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        If ((Local2 != 0x01))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local2, 0x01)
        }

        If ((Local3 != 0x01))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local3, 0x01)
        }

        PD11 [0x00] = 0x11
        Local2 = DerefOf (Local0)
        Local3 = DerefOf (Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        If ((Local2 != 0x11))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local2, 0x11)
        }

        If ((Local3 != 0x11))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local3, 0x11)
        }

        PD11 [0x00] = 0x01
        /* Uninitialized element: dereferencing it is a fatal error */

        Local1 = Local0 = PD11 [0x01]
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        Local2 = DerefOf (Local0)
        CH04 (__METHOD__, 0x01, 0x33, 0x00, __LINE__, 0x00, 0x00) /* AE_AML_UNINITIALIZED_ELEMENT */
        Local2 = DerefOf (Local1)
        CH04 (__METHOD__, 0x01, 0x33, 0x00, __LINE__, 0x00, 0x00) /* AE_AML_UNINITIALIZED_ELEMENT */
    }

    /* CopyObject */

    Method (MF14, 0, NotSerialized)
    {
        CopyObject (Local0 = PD11 [0x00], Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        Local2 = DerefOf (Local0)
        Local3 = DerefOf (Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        If ((Local2 != 0x01))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local2, 0x01)
        }

        If ((Local3 != 0x01))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local3, 0x01)
        }

        PD11 [0x00] = 0x11
        Local2 = DerefOf (Local0)
        Local3 = DerefOf (Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        If ((Local2 != 0x11))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local2, 0x11)
        }

        If ((Local3 != 0x11))
        {
            ERR (__METHOD__, ZFFF, __LINE__, 0x00, 0x00, Local3, 0x11)
        }

        PD11 [0x00] = 0x01
    }

    Method (MF15, 0, NotSerialized)
    {
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        CopyObject (Local0 = PD11 [0x01], Local1)
        CH03 (__METHOD__, 0x00, __LINE__, 0x00, 0x00)
        Local2 = DerefOf (Local0)
        CH04 (__METHOD__, 0x01, 0x33, 0x00, __LINE__, 0x00, 0x00) /* AE_AML_UNINITIALIZED_ELEMENT */
        Local2 = DerefOf (Local1)
        CH04 (__METHOD__, 0x01, 0x33, 0x00, __LINE__, 0x00, 0x00) /* AE_AML_UNINITIALIZED_ELEMENT */
    }

    Method (MF16, 0, NotSerialized)
    {
        If (0x01)
        {
            MF13 ()
        }

        If (0x01)
        {
            MF14 ()
        }

        If (0x01)
        {
            MF15 ()
        }
    }
