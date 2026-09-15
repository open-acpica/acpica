# Adding New Table Support To ACPICA
The instructions below cover adding support for a new ACPI table and its
subtables to ACPICA. While it strives to be accurate and comprehensive, some
details or corner cases may have been omitted. In those cases, look for hints in
the implementation of similar tables in the files mentioned below for hints.

## Naming convention
For the sake of generality, the four letter signature of the new table is
represented below by letter X. Accordingly, substitute the table signature for X
in the names of macros, functions or data structures. For instance, if the
signature of the new table is MADT, macro name ACPI_X_OFFSET() means
ACPI_MADT_OFFSET().

## Table header definitions
Header fields from the main table and the subtables (if they exist in this case)
need to be added to one of the actbl*.h files. There are three of these files,
numbered from 1 to 3, and there is no clear rule on which one should be used for
what. Since the definitions of the majority of new tables have been added to
actbl2.h recently, it can be used as the default location.

It is generally recommended to use the already defined tables for guidance on
where to add new ones. However, remember about the following:

1. Use ACPI_TABLE_HEADER for the type of the common part of the main header.
2. Define sub-tables separately from the main table.
3. If the given table is not defined in the ACPI specification proper, add a
   comment documenting its provenance and including a pointer to its definition.
4. Add ACPI_SIG_X entry for your table, it will later be used to connect
   compile and dump functions.

## Common part - setup
### ACPI_X_OFFSET
Add a new ACPI_X_OFFSET() to source/include/actbinfo.h. Main table offset
definitions are located at the top of the file, subtable definitions are located
down below. Match the ACPI_X_OFFSET() to the new table added to one of the
actbl*.h files.

### Subtables
Create an AcpiDmXSubnames[] array and populate it with the full names of the new
table subtables. This will come in handy when handling table dumping for human
readability.

Remember to add one additional entry for an unknown subtable, specifically in
this format:
```C
    "Unknown Subtable Type"         /* Reserved */
```

## Disassembler part
### DMTABLE
Entries for the main tables and for the subtables need to be added to
source/common/dmtbinfo*.c, in analogy to actbl*.h. Simply create entries in
the 'AcpiDmTableInfoX' format. Place the new entries in the matching dmtbinfo*.c
file in alphabetical order.

Don't forget to add ACPI_DMT_TERMINATOR at the end of each ACPI_DMTABLE_INFO.

Add the newly added AcpiDmTableInfoX to source/include/acdisasm.h with the
extern keyword - keep the array sorted alphabetically.

If any fields in the new table have a special type that defines some unique
values you need to specify a new ACPI entry type. For further instructions on
adding a new special type look to the chapters below.

### Special Dump Table function
Add function AcpiDmDumpX() to one of the source/common/dmtbdump*.c files
numbered from 1 to 3. Use the file in which the new function fits in
alphabetical order. Also add the header of the new function to
source/include/acdisasm.h.

This function is expected to utilize AcpiDmDumpTable() calls on main table and
subtables, catch errors or print more custom information that doesn't fit into
table definitions. For guidance, look at the existing tables with similar layout
and see how this custom dumping function is defined for them.

## Compiler part
### Special Compile function
Declare DtCompileX() in source/compiler/dtcompiler.h and add the function body
to source/compiler/dttable*.c numbered from 1 to 2. Use the file where the new
table fits alphabetically.

In the function body, use DtCompileTable() to compile the previously created
table formats. To connect tables to subtables, use DtPeekSubtable() and
DtInsertSubtable(). Custom compilation paths can be added, for example, based on
the ASL format read from PFieldList. Data can be inserted into tables elements
based on the complication process (for example, the subtable count field can be
used for counting subtables processed so far and the data can depend on that).
For details on how to connect subtables to the main table or how to modify table
elements, see the dttable*.c files.

## Common part - plugging functions
### Connection signature
Add an entry to the AcpiGbl_SupportedTables[] array in source/common/ahtable.c
using the ACPI_SIG_X signature added earlier.

### Connect functions to the signature
Add an entry to the AcpiDmTableData[] array in source/common/dmtable.c using
the ACPI_SIG_X signature added earlier. Don't fill out the last column
(Template) just yet - it is used for testing/browsing table formats and it will
be covered below.

If the new table has a simple static structure, only the TableInfo (second
column) is required - the third and fourth columns are not needed. However, if
your table has subtables or a more complex structure (like a variable amount of
data at the end of the main table) then the third and fourth columns are likely
necessary, as they connect custom compile and decompile functions that can
handle more complex table layouts.

With most complex tables, both the third and fourth columns need to be
added as a rule, but there are rare exceptions in which only one of them is
needed along with the second column entry (TableInfo). This is the case when the
given table is, for example, simple to dump (its size determines the trailing
data size), but complication requires custom code. For instance, it is the case
for the UEFI table.

## Tools support
Add all of the new structures previously added to actbl*.h to
source/tools/acpisrc/astable.c. Remember to keep the alphabetical order of
entries.

## Adding ACPI ENTRY TYPES
Add an *enum* for that type in ACPI_ENTRY_TYPES (located in
source/include/acdisasm.h).

### Dump table code
Modify AcpiDmDumpTable() in source/common/dmtable.c:

#### Set byte length
Add a case to the switch where the ByteLength variable is set

#### Printing
Add a case to the switch handling the dumping/printing part. If it's a number
type (like a flag), just print it using AcpiOsPrintf(). If for example, the type
includes subtable types, print the subtable names from a complementary
AcpiDmXSubnames[] name array. Generally, this part depends on what the type is -
look for similarities to it in the other cases in that switch statement.

One commonality is clamping down a subtable type using the
'Unknown Subtable Type' string in the Subnames string array. It may look as follows:

```C
Temp8 = *Target;
if (Temp8 > ACPI_X_RESERVED)
{
    Temp8 = ACPI_X_RESERVED;
}
```

ACPI_X_RESERVED needs to be present in actbl*.h where the subtable types are
listed, so if it hasn't been added to that file yet, do it now.

### Compiler support
Modify DtGetFieldLength() in source/compiler/dtutils.c - add a case to the
switch where the ByteLength variable is set.

## Adding the hex template
After adding support for the compiler and decompiler, a hex representation of
the new table needs to be generated. It can be used later in automated testing
of the compiler and decompiler consistency to ensure that they don't corrupt
data when used back-to-back in loops between ASL and AML.

First, create a field list .asl file with the new table based on its
documentation, in the one-line-per-field format: "[ByteLength] FieldName :
HexValue"

Some things to remember:
- FieldName strings should match the strings in the ACPI_DMTABLE_INFO entries.
- Signature field is the table name.
- Table Length and Checksum should be 0 - iasl will set them during compilation.
- Add all trailing data that can be present to maximize test coverage.

Below is a field list source example (main tables and trailing subtable pointer
data):
```asl
[0004]                          Signature : "XXXX"
[0004]                       Table Length : 00000000
[0001]                           Revision : 01
[0001]                           Checksum : 00
[0006]                             Oem ID : "YYYYY "
[0008]                       Oem Table ID : "Template"
[0004]                       Oem Revision : 00000001
[0004]                    Asl Compiler ID : "ZZZZ"
[0004]              Asl Compiler Revision : 00000000

[0004]               Number of Sub-tables : 00000001

[0001]                     Sub-table Type : 00
[0007]                           Reserved : 00 00 00 00 00 00 00
[0008]         Sub-table Physical Address : 0000000012340000
```

Next, build ACPICA from sources and use the newly built iasl binary to compile
the field list table file into the hex format:"
```bash
iasl -tc table_file.asl
```

Then, extract the unsigned char array from the table_file.hex output file and
add it to source/compiler/dttemplate.h with the 'const unsigned char
TemplateX[]' signature. Remember to keep the file in alphabetical order based on
the table name.

After adding the template to the header file you can now go back to
source/common/dmtable.c and add the template to the AcpiDmTableData[] entry for
the new table. Also declare the template with an extern keyword in
source/compiler/dtcompiler.h so it can be linked elsewhere.
