import pandas as pd
import numpy as np
import openpyxl




#read csv file
df1 = pd.read_csv("data.csv")
print(df1)



with pd.ExcelWriter("Rangefinder_testing.xlsx", engine="openpyxl", mode="a") as writer:
    df1.to_excel(writer, sheet_name="dataset2", index=False)

