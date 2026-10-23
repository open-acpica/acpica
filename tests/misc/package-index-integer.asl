// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0-only
/*
 * Package Index references used as integer operands through Locals and Args.
 * Compile: iasl package-index-integer.asl
 * Execute: acpiexec -b "execute MAIN" package-index-integer.aml
 * MAIN must return Zero. BADP, BADU, RADD, and LINC are negative tests.
 */
DefinitionBlock ("", "SSDT", 2, "LINUX", "IDXINT", 1)
{
    Name (FAIL, Zero)
    Name (PKG0, Package () { 0x0DAC, "DAC", Buffer () { 0xAC, 0x0D } })
    Name (UPKG, Package (2) { One })
    Name (NPKG, Package () { Package () { One } })

    Method (CHCK, 2)
    {
        If (Arg0 != Arg1)
        {
            Increment (FAIL)
            Debug = "Package Index integer test failed"
            Debug = Arg0
            Debug = Arg1
        }
    }

    Method (ADDI, 1)
    {
        Add (Arg0, 0x0A, Local0)
        Return (Local0)
    }

    Method (MAIN, 0, Serialized)
    {
        FAIL = Zero
        Store (0x0DAC, Index (PKG0, Zero))
        Local0 = Index (PKG0, Zero)
        Add (Local0, 0x0A, Local1)
        CHCK (Local1, 0x0DB6)
        CHCK (DerefOf (Local0), 0x0DAC)
        CHCK (DerefOf (Index (PKG0, Zero)), 0x0DAC)

        // Both input operands can refer to the same package element.
        Local1 = Index (PKG0, Zero)
        Add (Local0, Local1, Local2)
        CHCK (Local2, 0x1B58)

        // Preserve implicit conversion of strings and buffers to integers.
        Local1 = Index (PKG0, One)
        Add (Local1, 0x0A, Local2)
        CHCK (Local2, 0x0DB6)
        Local1 = Index (PKG0, 2)
        Add (Local1, 0x0A, Local2)
        CHCK (Local2, 0x0DB6)

        Subtract (Local0, 0x0A, Local2)
        CHCK (Local2, 0x0DA2)
        Multiply (Local0, 2, Local2)
        CHCK (Local2, 0x1B58)
        Divide (Local0, 10, Local2, Local3)
        CHCK (Local2, Zero)
        CHCK (Local3, 350)
        CHCK (ADDI (Local0), 0x0DB6)

        // Store and CopyObject must continue to copy the reference.
        Store (Local0, Local1)
        CopyObject (Local0, Local2)
        Store (42, Index (PKG0, Zero))
        CHCK (DerefOf (Local0), 42)
        CHCK (DerefOf (Local1), 42)
        CHCK (DerefOf (Local2), 42)

        // An arithmetic result replaces a Local target, not its referent.
        Add (Local0, 10, Local0)
        CHCK (Local0, 52)
        CHCK (DerefOf (Index (PKG0, Zero)), 42)
        Return (FAIL)
    }

    // A package cannot be implicitly converted to an integer.
    Method (BADP, 0)
    {
        Local0 = Index (NPKG, Zero)
        Return (Add (Local0, One))
    }

    // Report an uninitialized package element without dereferencing NULL.
    Method (BADU, 0)
    {
        Local0 = Index (UPKG, One)
        Return (Add (Local0, One))
    }

    // This change does not add implicit dereferencing of RefOf objects.
    Method (RADD, 0)
    {
        Local0 = One
        CHCK (Local0, One)
        Local1 = RefOf (Local0)
        Return (Add (Local1, One))
    }

    // Do not partially change Increment's reference-target semantics.
    Method (LINC, 0)
    {
        Local0 = Index (PKG0, Zero)
        Increment (Local0)
        Return (Local0)
    }
}
