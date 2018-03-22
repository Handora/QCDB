[CMU 15-445/645](..) {#cmu-15-445645 .logo}
====================

-   [Home](index.html)
-   [Assignments](../assignments.html)
-   [Schedule](../schedule.html)
-   [Syllabus](../syllabus.html)
-   [
    Youtube](https://www.youtube.com/playlist?list=PLSE8ODhjZXjYutVzTeAds8xUt1rcmyT7x "Youtube")
-   [ Canvas](https://canvas.cmu.edu/courses/1678 "Canvas")

[]{.icon .fa-database}

Project \#2 - B+Tree
--------------------

::: {.section .wrapper .style4 .container}
::: {.content}
::: {.section}
Overview
========

The second programming project is to implement an **index** in your
database system. The index is responsible for fast data retrieval
without having to search through every row in a database table,
providing the basis for both rapid random lookups and efficient access
of ordered records.

You will need to implement
[B+Tree](https://en.wikipedia.org/wiki/B%2B_tree) dynamic index
structure. It is a balanced tree in which the internal pages direct the
search and leaf pages contains actual data entries. Since the tree
structure grows and shrink dynamically, you are required to handle the
logic of split and merge. The project is comprised of the following
three tasks:

-   [**Task \#1 - B+Tree Pages**](#b+tree-pages)
-   [**Task \#2 - B+Tree Data Structure**](#b+tree-structure)
-   [**Task \#3 - Index Iterator**](#index-iterator)

This is a single-person project that will be completed individually
(i.e., no groups).

 Please post all of your questions about this project on Canvas. Do
**not** email the TAs directly with questions. The instructor and TAs
will **not** teach you how to debug your code.

-   **Release Date:** Oct 02, 2017
-   **Due Date:** Oct 25, 2017 @ 11:59pm

Project Specification
=====================

Like the first project, we are providing you with stub classes that
contain the API that you need to implement. You should **not** modify
the signatures for the pre-defined functions in these classes. If you do
this, then it will break the test code that we will use to grade your
assignment you end up getting no credit for the project. If a class
already contains certain member variables, you should **not** remove
them. But you may add private helper functions/member variables to these
classes in order to correctly realize the functionality.

The correctness of B+Tree index depends on the correctness of your
implementation of buffer pool, we will **not** provide solutions for the
previous programming projects. Since the second project is closely
related to the third project in which you will implement index crabbing
within existing B+ index, we have passed in a pointer parameter called
`transaction` with default value `nullptr`. You can safely ignore the
parameter for now; you don\'t need to change or call any functions
relate to the parameter.

Task \#1 - B+Tree Pages {#b+tree-pages}
-----------------------

You need to implement three Page classes to store the data of your
B+Tree tree.

-   [**B+Tree Parent Page**](#b+tree-page)
-   [**B+Tree Internal Page**](#b+tree-internal-page)
-   [**B+Tree Leaf Page**](#b+tree-leaf-page)

### B+Tree Parent Page {#b+tree-page}

This is the parent class that both the Internal Page and Leaf Page
inherited from and it only contains information that both child classes
share. The Parent Page is divided into several fields as shown by the
table below. All multi-byte fields are stored with the most significant
byte first (little-endian).

**B+Tree Parent Page Content**

  Variable Name   Size   Description
  --------------- ------ -----------------------------------------
  type            4      Page Type (internal or leaf)
  size            4      Number of Key & Value pairs in page
  max\_size       4      Max number of Key & Value pairs in page
  parent\_id      4      Parent Page Id
  page\_id        4      Self Page Id

You must implement your Parent Page in the designated files. You are
only allowed to modify the header file
(`src/include/page/b_plus_tree_page.h`{.filepath}) and its corresponding
source file (`src/page/b_plus_tree_page.cpp`{.filepath}).

### B+Tree Internal Page {#b+tree-internal-page}

Internal Page does not store any real data, but instead it stores an
ordered **m** key entries and **m+1** child pointers (a.k.a page\_id).
Since the number of pointer does not equal to the number of key, the
first key is set to be invalid, and lookup methods should always start
with the second key. At any time, each internal page is at least half
full. During deletion, two half-full pages can be joined to make a legal
one or can be redistributed to avoid merging, while during insertion one
full page can be split into two.

You must implement your Internal Page in the designated files. You are
only allowed to modify the header file
(`src/include/page/b_plus_tree_internal_page.h`{.filepath}) and its
corresponding source file
(`src/page/b_plus_tree_internal_page.cpp`{.filepath}).

### B+Tree Leaf Page {#b+tree-leaf-page}

The Leaf Page stores an ordered **m** key entries and **m** value
entries. In your implementation, value should only be 64-bit record\_id
that is used to locate where actual tuples are stored, see `RID` class
defined under in `src/include/common/rid.h`{.filepath}. Leaf pages have
the same restriction on the number of key/value pairs as Internal pages,
and should follow the same operations of merge, redistribute and split.

You must implement your Internal Page in the designated files. You are
only allowed to modify the header file
(`src/include/page/b_plus_tree_leaf_page.h`{.filepath}) and its
corresponding source file
(`src/page/b_plus_tree_leaf_page.cpp`{.filepath}).

**Important:**Even though the Leaf Pages and Internal Pages contain the
same type of key, they may have distinct type of value, thus the
`max_size` of leaf and internal pages could be different. You should
calculate the `max_size` within both Internal Page\'s and Leaf Page\'s
`Init()` method.

Each B+Tree leaf/internal page corresponds to the content (i.e., the
byte array `data_`) of a memory page fetched by buffer pool. So every
time you try to read or write a leaf/internal page, you need to first
**fetch** the page from buffer pool using its unique `page_id`, then
[reinterpret
cast](http://en.cppreference.com/w/cpp/language/reinterpret_cast)to
either a leaf or an internal page, and unpin the page after any writing
or reading operations.

Task \#2 - B+Tree Data Structure {#b+tree-structure}
--------------------------------

Your B+Tree Index could only support **unique key**. That is to say,
when you try to insert a key-value pair with duplicate key into the
index, it should not perform the insertion and return false.

Your B+Tree Index is required to correctly perform split if insertion
triggers current number of key/value pairs exceeds `max_size`. It also
needs to correctly perform merge or redistribute if deletion cause
certain page to go below the occupancy threshold. Since any write
operation could lead to the change of `root_page_id` in B+Tree index, it
is your responsibility to update `root_page_id` in the header page
(`src/include/page/header_page.h`{.filepath}) to ensure that the index
is durable on disk. Within the `BPlusTree` class, we have already
implemented a function called `UpdateRootPageId()` for you; all you need
to do is invoke this function whenever the `root_page_id` of B+Tree
index changes.

Your B+Tree implementation must hide the details of the key/value type
and associated comparator, like this:

    template <typename KeyType,
              typename ValueType,
              typename KeyComparator>
    class BPlusTree{
       // ---
    };

These classes are already implemented for you:

-   `KeyType`: The type of each key in the index. This will only be
    `GenericKey`, the actual size of `GenericKey` is specified and
    instantiated with a template argument and depends on the data type
    of indexed attribute.
-   `ValueType`: The type of each value in the index. This will only be
    64-bit RID.
-   `KeyComparator`: The class used to compare whether two `KeyType`
    instances are less/greater-than each other. These will be included
    in the `KeyType` implementation files.

Task \#3 - Index Iterator {#index-iterator}
-------------------------

You will build a general purpose index iterator to retrieve all the leaf
pages efficiently. The basic idea is to organize them into a single
linked list, and then traverse every key/value pairs in specific
direction stored within the B+Tree leaf pages. Your index iterator
should follow the functionality of
[Iterator](http://www.cplusplus.com/reference/iterator/) defined in
c++11, including the ability to iterate through a range of elements
using a set of operators (with at least the increment and dereference
operators).

You must implement your index iterator in the designated files. You are
only allowed to modify the header file
(`src/include/index/index_iterator.h`{.filepath}) and its corresponding
source file (`src/index/index_iterator.cpp`{.filepath}). You do not need
to modify any other files. You **must** implement the following
functions in the `IndexIterator` class found in these files. In the
implementation of index iterator, you are allowed to add any helper
methods as long as you have those three methods below.

-   `isEnd()`: Return whether this iterator is pointing at the last
    key/value pair.
-   `operator++()`: Move to the next key/value pair.
-   `operator*()`: Return the key/value pair this iterator is currently
    pointing at.

Instructions
============

Setting Up Your Development Environment
---------------------------------------

Download the project source code
[here](%7Bfilename%7D/files/sqlite-fall2017.tar.gz). This version has
additional files that were added after project \#1 so you need to update
your local copy.

Make sure that your source code has the following
`VERSION.txt`{.filepath} file:

    Created: Oct 02 2017 @ 22:32:09
    Last Commit: 250b733e4b16493caea9f3310dfebf1029fdceae

Testing
-------

You can test the individual components of this assigment using our
testing framework. We use [GTest](https://github.com/google/googletest)
for unit test cases. You can compile and run each test individually from
the command-line:

    cd build
    make b_plus_tree_test
    ./test/b_plus_tree_test

In the b\_plus\_tree\_print\_test, you can print out the internal data
structure of your b+ tree index, it\'s an intuitive way to track and
find early mistakes.

    cd build
    make b_plus_tree_print_test
    ./test/b_plus_tree_print_test

**Important:** These tests are only a subset of the all the tests that
we will use to evaluate and grade your project. You should write
additional test cases on your own to check the complete functionality of
your implementation.

Development Hints
-----------------

Instead of using `printf` statements for debugging, use the `LOG_*`
macros for logging information like this:

    LOG_INFO("# Leaf Page: %s", leaf_page->ToString().c_str());
    LOG_DEBUG("Fetching page %d", page_id);

To enable logging in your project, you will need to reconfigure it like
this:

    cd build
    cmake -DCMAKE_BUILD_TYPE=DEBUG ..
    make

The different logging levels are defined in
`src/include/common/logger.h`{.filepath}. After enabling logging, the
logging level defaults to `LOG_LEVEL_INFO`. Any logging method with a
level that is equal to or higher than `LOG_LEVEL_INFO` (e.g.,
`LOG_INFO`, `LOG_WARN`, `LOG_ERROR`) will emit logging information.

Using [assert](http://www.cplusplus.com/reference/cassert/assert/)to
force check the correctness of your index implementation. For example,
when you try to delete a page, the `page_count` must equals to 0.

Using a relatively small value of page size at the beginning test stage,
it would be easier for you to check whether you have done the delete and
insert in the correct way. You can change the page size in configuration
file (`src/include/common/config.h`{.filepath}).

Grading Rubric
==============

Each project submission will be graded based on the following criteria:

1.  Does the submission successfully execute all of the test cases and
    produce the correct answer?
2.  Does the submission execute without any memory leaks?

Note that we will use additional test cases that are more complex and go
beyond the sample test cases that we provide you.

Late Policy
===========

See the [late policy](#late-policy) in the syllabus.

Submission
==========

After completing the assignment, you can submit your implementation of
to Autolab:

-   <https://autolab.andrew.cmu.edu/courses/15445-f17>.

You only need to include the following files:

-   Every file for Project 1 (6 in total)
-   `src/include/page/b_plus_tree_page.h`{.filepath}
-   `src/page/b_plus_tree_page.cp`{.filepath}
-   `src/include/page/b_plus_tree_internal_page.h`{.filepath}
-   `src/page/b_plus_tree_internal_page.cp`{.filepath}
-   `src/include/page/b_plus_tree_leaf_page.h`{.filepath}
-   `src/page/b_plus_tree_leaf_page.cp`{.filepath}
-   `src/include/index/b_plus_tree.h`{.filepath}
-   `src/index/b_plus_tree.cpp`{.filepath}
-   `src/include/index/index_iterator.h`{.filepath}
-   `src/index/index_iterator.cpp`{.filepath}

You can submit your answers as many times as you like and get immediate
feedback. Your score will be sent via email to your Andrew account
within a few minutes after your submission.

Collaboration Policy
====================

-   Every student has to work individually on this assignment.
-   Students are allowed to discuss high-level details about the project
    with others.
-   Students are **not** allowed to copy the contents of a white-board
    after a group meeting with other students.
-   Students are **not** allowed to copy the solutions from another
    colleague.

 **WARNING:** All of the code for this project must be your own. You may
not copy source code from other students or other sources that you find
on the web. Plagiarism **will not** be tolerated. See CMU\'s [Policy on
Academic
Integrity](http://www.cmu.edu/policies/documents/Academic%20Integrity.htm)
for additional information.

::: {.published}
**Last Updated:** Oct 23, 2017
:::
:::
:::
:::

[
[![](../images/cmu-db-group.png)](http://db.cs.cmu.edu "Carnegie Mellon Database Group"){.image-link}
]{.copyright}
