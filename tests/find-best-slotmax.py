import pandas as pd
import sys
import os

def process_csv(input_file, output_file=None):
    # If no output file is provided, use a default name "result.csv" in the same directory
    if output_file is None:
        output_file = os.path.join(os.path.dirname(input_file), "result.csv")

    # Step 1: Read the file into a pandas DataFrame
    df = pd.read_csv(input_file, sep='\t')

    # Step 2: Extract rows with the highest value in "Threads" column
    max_threads = df['Threads'].max()
    df_max_threads = df[df['Threads'] == max_threads]

    # Step 3: Extract rows with "InsertP"=100 from the result of Step 2
    df_insertp_100 = df_max_threads[df_max_threads['InsertP'] == 100]

    # Step 4: Group and sort by "MplThrh", "Dist", "ValSize"
    df_grouped = df_insertp_100.groupby(['Dist', 'ValSize', 'MplThrh'])

    # Step 5: Find the highest "Mops/s" value for each group
    df_max_mops = df_grouped['Mops/s'].max().reset_index()

    # Step 6: Merge with the original DataFrame to get the corresponding "SlotMax" values
    df_result = pd.merge(df_max_mops, df_insertp_100, on=['Dist', 'ValSize', 'MplThrh', 'Mops/s'], how='left')

    # Keep only necessary columns
    df_result = df_result[['Dist', 'ValSize', 'MplThrh', 'SlotMax', 'Mops/s', 'Threads']]

    # Rename the "Mops/s" column to "Inst_Mops/s"
    df_result = df_result.rename(columns={'Mops/s': 'Inst_Mops/s'})

    # Step 6: Replace "MplThrh" values: 0 -> "MaplTree", 100 -> "BTree"
    #df_result['MplThrh'] = df_result['MplThrh'].replace({0: 'MaplTree', 100: 'BTree'})

    # Save the result to a CSV file
    df_result.to_csv(output_file, index=False)

    print(f"Processing complete. Output saved to {output_file}")

# Main function to handle command line arguments
if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python script.py <input_file> [output_file]")
        sys.exit(1)

    input_file = sys.argv[1]

    # If an output file is provided as an argument, use it
    output_file = sys.argv[2] if len(sys.argv) > 2 else None

    # Process the CSV file
    process_csv(input_file, output_file)