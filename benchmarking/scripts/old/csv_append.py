import csv 

def append_csv(target_file, source_file):
    with open(target_file, "a", newline="") as file_target:
        writer = csv.writer(file_target)

        with open(source_file, "r") as file_source:
            reader = csv.reader(file_source)
            for row in reader:
                writer.writerow(row)

append_csv("2-20cm_data.csv", "data.csv")

