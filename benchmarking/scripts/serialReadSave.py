import serial.tools.list_ports
import serial
import time
import csv
import sys

args = sys.argv
FILE = args[1]
NUM_TRIALS_PER = int(args[2])
NUM_TRIALS_TOT = int(args[3])

def main():
    # setup
    print_ports()
    f = open_file(FILE)

    # create columns
    with open(FILE, "a", newline='') as file:
        writer = csv.writer(f, delimiter=",")
        writer.writerow(["trialNum", "reading"])

    # intialize serial communication
    serialCom = open_serial()

    # take data, give user a couple seconds to move the apparatus
    k = 0
    while k < NUM_TRIALS_TOT:
        get_reading(f, NUM_TRIALS_PER, serialCom, k + 1)
        # delay for sensor adjustment, 7 seconds right now
        for n in range(7, 0, -1):
            print(f'Measure in {n}')
            time.sleep(1)
        k += 1

    # clean up
    f.close()

# list which port is being used
def print_ports():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        print(port)

#create new csv file for data
def open_file(file_name: str):

    f = open(file_name, "w", newline="")
    f.truncate()
    return f

def open_serial():
    # communicate with serial port connected to arduino
    serialCom = serial.Serial("COM8", 9600)
    serialCom.setDTR(False)
    time.sleep(1)
    serialCom.flushInput()
    serialCom.setDTR(True)
    return serialCom


def get_reading(file, num_trials, serialCom, trial):
    serialCom.write(b'START\n')
    num_trials = 20
    for k in range(num_trials):
        try:
            s_bytes = serialCom.readline()
            decoded_bytes = s_bytes.decode("utf-8").strip('\r\n')
            
            if k == 0:
                values = decoded_bytes.split()
            else:
                values = [float(x) for x in decoded_bytes.split()]
            row = [trial, values[0]]
            writer = csv.writer(file, delimiter=",")
            writer.writerow(row)

            print(values)

        except:
            print("Line not recorded")
    serialCom.write(b'STOP\n')

if __name__ == "__main__":
    main()
