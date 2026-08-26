# Contributing To The ACPICA Project

## Submitting a Contribution

First of all, consider the license.  It is a dual license, so your contribution
may be included in downstream projects under any of the two alternative
licenses.

If you are properly authorized to contribute, the preferred way
to do so is by submitting a pull request (PR) to the
[project's reposibory on GitHub](https://github.com/open-acpica/acpica).
An alternative way is to send patches to the <acpica-devel@lists.linux.dev>
development mailing list.

## Formal Requirements For Contributions

A contribution typically consists of one or more *git* commits that each
represent a set of content changes equivalent to a patch.  Every *git* commit
included in a contribution (or every patch in the email submission case)
**is required** to carry a Signed-off-by tag representing its author's
[Developer Certificate of Origin (DCO)](https://developercertificate.org/).
Contributions that fail to meet this requirement cannot be accepted.

If a given commit (or patch) is not submitted directly by the author of the
changes represented by it, the person submitting it should also add their own
DCO represented by a Signed-off-by tag to it.

Every *git* commit included in a contribution (or every patch in the email
submission case) **needs** to contain a subject and a changelog.  The subject is
a single line summary of the change that will be present in the output of the
"*git log --oneline*" command (or equivalent).  The changelog is the free form
text following the subject in the output of the "*git log*" command (or
equivalent).  The Signed-off-by tags mentioned above should be located after the
changelog.

If the subject or the changelog is missing from at least one commit (or patch)
included in a contribution, the project maintainers may refuse to accept it as
a whole.  However, if they are provided with enough information to be able to
add the missing parts by themselves, they may decide to do so at their
discretion.

The commit (or patch) changelog **is required** to explain the motivation for
making the given change (that is, the reason for making it, or its goal, or
purpose), or it will be regarded as a missing one.  In addition to that, it
should also explain how the specific code modifications, for instance, serve to
achieve the stated goal (if that is not obvious), what alternative ways of
achieving the goal have been considered (if any) and why they are regarded as
inferior.

## Coding Style

Code updates need to follow the project's coding style which is consistent
across the entire code base.  The most straightforward way to achieve that is to
follow the style of the existing code when modifying it.

## Testing

Every contribution is expected to be tested by it submitter.  The level of
testing that is adequate for a given change generally depends on how complex
and intrusive the change is, but it also depends on what componentes are
affected.  For example, changes that only affect the ASL compiler and changes
that only affect the AML interpreter generally need to be tested differently.

Every contribution will also undergo maintainer testing or CI (automated)
testing and if it does not pass that testing, its submitter will be asked to
revise it until it is free of defects that cause the tests to fail.
