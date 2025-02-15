import subprocess

def run_script(commands):
    """Runs the database program with given commands and captures output."""
    output = []
    
    # Start the database program
    process = subprocess.Popen(
        ["program.exe", "test"],  # No `./` on Windows
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
    "insert 18 user18 person18@example.com",
    "insert 7 user7 person7@example.com",
    "insert 10 user10 person10@example.com",
    "insert 29 user29 person29@example.com",
    "insert 23 user23 person23@example.com",
    "insert 4 user4 person4@example.com",
    "insert 14 user14 person14@example.com",
    "insert 30 user30 person30@example.com",
    "insert 15 user15 person15@example.com",
    "insert 26 user26 person26@example.com",
    "insert 22 user22 person22@example.com",
    "insert 19 user19 person19@example.com",
    "insert 2 user2 person2@example.com",
    "insert 1 user1 person1@example.com",
    "insert 21 user21 person21@example.com",
    "insert 11 user11 person11@example.com",
    "insert 6 user6 person6@example.com",
    "insert 20 user20 person20@example.com",
    "insert 5 user5 person5@example.com",
    "insert 8 user8 person8@example.com",
    "insert 9 user9 person9@example.com",
    "insert 3 user3 person3@example.com",
    "insert 12 user12 person12@example.com",
    "insert 27 user27 person27@example.com",
    "insert 17 user17 person17@example.com",
    "insert 16 user16 person16@example.com",
    "insert 13 user13 person13@example.com",
    "insert 24 user24 person24@example.com",
    "insert 25 user25 person25@example.com",
    "insert 28 user28 person28@example.com",
    "insert 31 user31 person31@example.com",
    "insert 32 user32 person32@example.com",
    "insert 33 user33 person33@example.com",
    "insert 34 user34 person34@example.com",
    "insert 35 user35 person35@example.com",
    "insert 36 user36 person36@example.com",
    "insert 37 user37 person37@example.com",
    "insert 38 user38 person38@example.com",
    "insert 39 user39 person39@example.com",
    "insert 40 user40 person40@example.com",
    "insert 41 user41 person41@example.com",
    "insert 42 user42 person42@example.com",
    "insert 43 user43 person43@example.com",
    "insert 44 user44 person44@example.com",
    "insert 45 user45 person45@example.com",
    "insert 46 user46 person46@example.com",
    "insert 47 user47 person47@example.com",
    "insert 48 user48 person48@example.com",
    "insert 49 user49 person49@example.com",
    "insert 50 user50 person50@example.com",
    "insert 51 user51 person51@example.com",
    "insert 52 user52 person52@example.com",
    "insert 53 user53 person53@example.com",
    "insert 54 user54 person54@example.com",
    "insert 55 user55 person55@example.com",
    ".btree",
    ".exit"
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
