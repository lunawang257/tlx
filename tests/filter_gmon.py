import re
import sys

def process_line(line):
    # Step 1: Remove any occurrences of '(unsigned short)'
    line = line.replace('(unsigned short)', '')

    # Step 2: Split the line using two or more spaces as the separator
    tokens = re.split(r'\s{2,}', line.strip())

    # Step 3: Process the last token
    if tokens:
        last_token = tokens[-1]
        # Find the first occurrence of '(' and the substring before it
        if '(' in last_token:
            last_part = last_token[:last_token.find('(')]
            # Find the last space and keep the part after it
            if ' ' in last_part:
                last_part = last_part[last_part.rfind(' ') + 1:]
            tokens[-1] = '>::' + last_part

    # Rejoin the tokens with exactly 5 spaces
    return '     '.join(tokens)

def process_file():
    for line in sys.stdin:
        processed_line = process_line(line)
        print(processed_line)

# Call the process_file function to handle the input from stdin and output to stdout
if __name__ == "__main__":
    process_file()