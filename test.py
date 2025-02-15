import subprocess

def run_script(commands):
    """Runs the database program with given commands and captures output."""
    output = []
    
    # Start the database program
    process = subprocess.Popen(
        ["program.exe", "test1"],  # No `./` on Windows
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Send each command to the program
    for command in commands:
        process.stdin.write(command + "\n")
    process.stdin.close()  # Close input stream

    # Read output lines
    output = process.stdout.readlines()
    process.stdout.close()
    
    return [line.strip() for line in output]

def test_duplicate_id():
    """Test inserting a duplicate ID should return an error."""
    script = [
        "insert 100 user100 person1@example.com",
        "insert 2 user2 person1@example.com",
        "insert 3 user3 person1@example.com",
        "insert 4 user4 person1@example.com",
        "insert 5 user5 person1@example.com",
        "insert 6 user6 person1@example.com",
        "insert 7 user7 person1@example.com",
        "insert 8 user8 person1@example.com",
        "insert 9 user9 person1@example.com",
        "insert 99 user99 person1@example.com",
        "insert 111 user111 person1@example.com",
        "insert 112 user112 person1@example.com",
        "insert 113 user113 person1@example.com",
        "insert 114 user114 person1@example.com",
        # "insert 115 user115 person1@example.com",
        # "insert 116 user116 person1@example.com",
        # "insert 117 user117 person1@example.com",
        # "insert 118 user117 person1@example.com",
        # "insert 119 user117 person1@example.com",
        # "insert 120 user117 person1@example.com",
        # "insert 121 user117 person1@example.com",
        # "insert 122 user117 person1@example.com",
        # "insert 123 user117 person1@example.com",
        # "insert 124 user117 person1@example.com",
        # "insert 125 user117 person1@example.com",
        # "insert 126 user117 person1@example.com",
        # "insert 127 user117 person1@example.com",
        # "insert 128 user117 person1@example.com",
        # "insert 129 user117 person1@example.com",
        # "insert 130 user117 person1@example.com",
        # "insert 131 user117 person1@example.com",
        "insert 88 user88 person1@example.com",
        ".exit",
    ]    
    # upadted query: update user2 personal@hhs.com where id=2 
    expected_output = [
        "db > Executed.",                      # First insert works
        "db > Error: Duplicate key.",          # Second insert fails
        "db > (1, user1, person1@example.com)", # Select shows one row
        "Executed.",
        "db > ",
    ]

    result = run_script(script)

    # Check if the actual output matches expected output
    assert result == expected_output, f"Test Failed! Got: {result}"
    print("✅ Test Passed: Duplicate ID correctly detected!")

if __name__ == "__main__":
    test_duplicate_id()
