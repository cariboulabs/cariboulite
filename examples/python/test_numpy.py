import numpy as np
import matplotlib.pyplot as plt

#a = np.arange(15).reshape(3,5)
a = np.arange(15)
plt.plot(a, np.sin(a))
plt.ylabel('some numbers')
plt.show()
