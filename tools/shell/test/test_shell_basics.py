import os

import pexpect
import pytest

from conftest import ShellTest
from test_helper import deleteIfExists


def test_basic(temp_db) -> None:
    test = ShellTest().add_argument(temp_db).statement('RETURN "databases rule" AS a;')
    result = test.run()
    result.check_stdout("\u2502 databases rule \u2502")


def test_range(temp_db) -> None:
    test = ShellTest().add_argument(temp_db).statement("RETURN RANGE(0, 10) AS a;")
    result = test.run()
    result.check_stdout("[0,1,2,3,4,5,6,7,8,9,10]")


@pytest.mark.parametrize(
    ("input", "output"),
    [
        ("RETURN LIST_CREATION(1,2);", "[1,2]"),
        ("RETURN STRUCT_PACK(x:=2,y:=3);", "{x: 2, y: 3}"),
        ("RETURN STRUCT_PACK(x:=2,y:=LIST_CREATION(1,2));", "{x: 2, y: [1,2]}"),
    ],
)
def test_nested_types(temp_db, input, output) -> None:
    test = ShellTest().add_argument(temp_db).statement(input)
    result = test.run()
    result.check_stdout(output)


def test_invalid_cast(temp_db) -> None:
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement("CREATE NODE TABLE a(i STRING, PRIMARY KEY(i));")
        .statement("CREATE (:a {i: '****'});")
        .statement('MATCH (t:a) RETURN CAST(t.i, "INT8");')
    )
    result = test.run()
    result.check_stdout(
        'Error: Conversion exception: Cast failed. Could not convert "****" to INT8.',
    )


def test_enter_in_between_input(temp_db) -> None:
    test = ShellTest().add_argument(temp_db)
    test.start()
    test.send_statement("CREATE NODE TABLE Test (id INT64 PRIMARY KEY);")
    test.send_statement("\x1b[D" * 5)  # left arrow
    test.send_control_statement("j")  # ctrl + j
    assert (
        test.shell_process.expect_exact(
            ["\u2502 Table Test has been created. \u2502", pexpect.EOF]
        )
        == 0
    )


def test_multiline(temp_db) -> None:
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement("RETURN")
        .statement('"databases rule"')
        .statement("AS")
        .statement("a")
        .statement(";")
    )
    result = test.run()
    result.check_stdout("\u2502 databases rule \u2502")


def test_multi_queries_one_line(temp_db) -> None:
    # two successful queries
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement('RETURN "databases rule" AS a; RETURN "kuzu is cool" AS b;')
    )
    result = test.run()
    result.check_stdout("\u2502 databases rule \u2502")
    result.check_stdout("\u2502 kuzu is cool \u2502")

    # one success one failure
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement('RETURN "databases rule" AS a; RETURN s;')
    )
    result = test.run()
    result.check_stdout("\u2502 databases rule \u2502")
    result.check_stdout("Error: Binder exception: Variable s is not in scope.")

    # two failing queries
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement('RETURN "databases rule" S a; RETURN s;')
    )
    result = test.run()
    result.check_stdout(
        [
            "Error: Parser exception: Invalid input < S>: expected rule ku_Statements (line: 1, offset: 24)",
            '"RETURN "databases rule" S a; RETURN s;"',
            "                         ^",
        ],
    )


def test_row_truncation(temp_db, csv_path) -> None:
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement(f'LOAD FROM "{csv_path}" (HEADER=true) RETURN id;')
    )
    result = test.run()
    result.check_stdout("(21 tuples, 20 shown)")
    result.check_stdout(
        [
            "\u2502  \u00b7    \u2502",
            "\u2502  \u00b7    \u2502",
            "\u2502  \u00b7    \u2502",
        ]
    )


def test_column_truncation(temp_db, csv_path) -> None:
    # width when running test is 80
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement(f'LOAD FROM "{csv_path}" (HEADER=true) RETURN *;')
    )
    result = test.run()
    result.check_stdout("id")
    result.check_stdout("fname")
    result.check_stdout("Gender")
    result.check_stdout("\u2502 ... \u2502")
    result.check_stdout("courseScoresPerTerm")
    result.check_stdout("(13 columns, 4 shown)")

    # test column name truncation
    long_name = "a" * 100
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement(f'RETURN "databases rule" AS {long_name};')
    )
    result = test.run()
    result.check_stdout(f"\u2502 {long_name[:73]}... \u2502")
    result.check_stdout("\u2502 databases rule")


def long_messages(temp_db) -> None:
    long_message = "-" * 4096
    test = ShellTest().add_argument(temp_db).statement(f'RETURN "{long_message}" AS a;')
    result = test.run()
    result.check_stdout(long_message)


def test_history_consecutive_repeats(temp_db, history_path) -> None:
    test = (
        ShellTest()
        .add_argument(temp_db)
        .add_argument("-p")
        .add_argument(history_path)
        .statement('RETURN "databases rule" AS a;')
        .statement('RETURN "databases rule" AS a;')
    )
    result = test.run()
    result.check_stdout("\u2502 databases rule \u2502")

    with open(os.path.join(history_path, "history.txt")) as f:
        assert f.readline() == 'RETURN "databases rule" AS a;\n'
        assert f.readline() == ""

    deleteIfExists(os.path.join(history_path, "history.txt"))


def test_kuzurc(temp_db) -> None:
    deleteIfExists(".kuzurc")
    # confirm that nothing is read on startup
    test = ShellTest().add_argument(temp_db)
    result = test.run()
    result.check_not_stdout("-- Processing: .kuzurc")

    # create a .kuzurc file
    with open(".kuzurc", "w") as f:
        f.write("CREATE NODE TABLE a(i STRING, PRIMARY KEY(i));\n")
        f.write(":max_rows 1\n")

    # confirm that the file is read on startup
    test = ShellTest().add_argument(temp_db).statement("CALL show_tables() RETURN *;")
    result = test.run()
    deleteIfExists(".kuzurc")
    result.check_stdout("-- Processing: .kuzurc")
    result.check_stdout("maxRows set as 1")
    result.check_stdout("a")

    # create a .kuzurc file with errors
    with open(".kuzurc", "w") as f:
        f.write('RETURN "databases rule" S a; RETURN s;\n')
        f.write(":max_rows\n")
        f.write(":mode table\n")
        f.write("CREATE NODE TABLE b(i STRING, PRIMARY KEY(i));\n")

    # confirm that the file is read on startup
    test = ShellTest().add_argument(temp_db).statement("CALL show_tables() RETURN *;")
    result = test.run()
    deleteIfExists(".kuzurc")
    result.check_stdout("-- Processing: .kuzurc")
    result.check_stdout(
        [
            "Error: Parser exception: Invalid input < S>: expected rule ku_Statements (line: 1, offset: 24)",
            '"RETURN "databases rule" S a; RETURN s;"',
            "                         ^",
        ],
    )
    result.check_stdout("Cannot parse '' as number of rows. Expect integer.")
    result.check_stdout("mode set as table")
    result.check_stdout("b")


def test_comments(temp_db) -> None:
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement(
            'RETURN // testing\n /* test\ntest\ntest */"databases rule" // testing\n AS a\n; // testing'
        )
        .statement(
            "\x1b[A\r"
        )  # run the last command again to make sure comments are still ignored
    )
    result = test.run()
    result.check_stdout("\u2502 databases rule \u2502")


def test_shell_auto_completion(temp_db) -> None:
    test = ShellTest().add_argument(temp_db)
    test.start()
    test.send_statement(
        "CREATE NODE TABLE coolTable(i STRING, longPropertyName STRING, autoCompleteUINT8 UIN\t"
    )
    test.send_statement("\t")
    test.send_statement("\t")
    test.send_statement("\t")
    test.send_statement(", PRIMARY KEY(i));\n")
    assert (
        test.shell_process.expect_exact(
            ["\u2502 Table coolTable has been created. \u2502", pexpect.EOF]
        )
        == 0
    )

    test.send_statement("ma\t")
    test.send_statement("\t")
    test.send_statement("\t")
    test.send_statement("\x1b[Z")
    test.send_statement(" (n:\t")
    test.send_statement(") ret\t")
    test.send_statement(" n.\t")
    test.send_statement("\t")
    test.send_finished_statement(";\r")
    assert (
        test.shell_process.expect_exact(
            ["\u2502 n.longPropertyName \u2502", pexpect.EOF]
        )
        == 0
    )

    test.send_statement("c\t")
    test.send_statement(" show_t\t")
    test.send_statement("() ret\t")
    test.send_finished_statement(" *;\n")
    assert (
        test.shell_process.expect_exact(["\u2502 coolTable \u2502", pexpect.EOF]) == 0
    )

    test.send_statement("match (a:coolTable)-[]->()-[]->(a:coolTable) ")
    test.send_statement("r")
    test.send_statement("eturn a.lon\t")
    test.send_finished_statement(";\n")
    assert test.shell_process.expect_exact(["(0 tuples)", pexpect.EOF]) == 0


def test_shell_unicode_input(temp_db) -> None:
    test = (
        ShellTest()
        .add_argument(temp_db)
        .statement(
            "CREATE NODE TABLE IF NOT EXISTS `B\\u00fccher` (title STRING, price INT64, PRIMARY KEY (title));\n"
        )
        .statement(
            "CREATE (n:`B\\u00fccher` {title: 'Der Thron der Sieben K\\00f6nigreiche'}) SET n.price = 20;\n"
        )
        .statement("MATCH (n:B\\u00fccher) RETURN label(n);\n")
        .statement(
            'return "\\uD83D\\uDE01";\n'
        )  # surrogate pair for grinning face emoji
        .statement('return "\\U0001F601";\n')  # grinning face emoji
        .statement('return "\\uD83D";\n')  #  unmatched surrogate pair
        .statement('return "\\uDE01";\n')  # unmatched surrogate pair
        .statement('return "\\uD83D\\uDBFF";\n')  # bad lower surrogate
        .statement('return "\\u000";\n')  # bad unicode codepoint
        .statement('return "\\u0000";\n')  # Null character
        .statement('return "\\U00110000";\n')  # Invalid codepoint
    )
    result = test.run()
    result.check_stdout("\u2502 B\u00fccher")
    result.check_stdout("\u2502 \U0001f601")  # grinning face emoji
    result.check_stdout("\u2502 \U0001f601")  # grinning face emoji
    result.check_stdout("Error: Unmatched high surrogate")
    result.check_stdout("Error: Failed to convert codepoint to UTF-8")
    result.check_stdout("Error: Invalid surrogate pair")
    result.check_stdout(
        'Error: Parser exception: Invalid input <return ">: expected rule oC_RegularQuery (line: 1, offset: 7)'
    )
    result.check_stdout("Error: Null character not allowed")
    result.check_stdout("Error: Invalid Unicode codepoint")
