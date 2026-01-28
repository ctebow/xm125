import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

data = pd.read_csv("data.csv")

D = data.to_numpy()



def average_static_distance(arr):
    total1 = 0
    total2 = 0
    
    for val in arr:
        total1 += val[1]
        total2 += val[2]
    
    result = (total1 / len(arr), total2 / len(arr))

    return result
    


