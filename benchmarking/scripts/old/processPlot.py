import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import csv

data1 = pd.read_csv("data.csv")

D = data1.to_numpy()


#t = D[:,0]
distance1 = D[:,0]
distance2 = D[:,1]

plt.plot(distance1, distance2)
#plt.plot(t, distance2)
plt.show()

