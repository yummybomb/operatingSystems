import sys
import subprocess
import time
import shutil
import os
import time

DEBUG = False

def deleteAndCompile():
    commands = [
        ["fusermount", "-u", "mountdir"],
        ["make", "clean"],
        ["rm", "-rf", "mountdir"],
        ["mkdir", "mountdir"],
        ["rmDiskfile"],
        ["make"],
        ["./rufs","-s","mountdir"]
    ]
    for command in commands:
        if command[0] == "rmDiskfile":
            if os.path.exists('DISKFILE'): os.remove('DISKFILE')
        else:
            result = subprocess.run(command, capture_output=True, text=True)
            if DEBUG: print(result.stdout)
        
deleteAndCompile()
print("SIMPLE TEST: \n")
start_time = time.time()
result = subprocess.run(["./benchmark/simple_test"], capture_output=True, text=True)
end_time = time.time()
print(end_time - start_time)
print(result.stdout)

deleteAndCompile()
print("TEST CASE TEST: \n")
start_time = time.time()
result = subprocess.run(["./benchmark/test_case"], capture_output=True, text=True)
end_time = time.time()
print(end_time - start_time)
print(result.stdout)






