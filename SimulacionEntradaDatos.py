import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from tensorflow.lite.python.interpreter import Interpreter

# Cargar el modelo TFLite
interpreter = Interpreter(model_path="Entrenamiento/NetSaturada50cm/modelo_5_loss-0_0954_acc-0_9800.tflite")
interpreter.allocate_tensors()

# Cargar datos desde CSV
df = pd.read_csv("datos_R2D2_2025-03-09_13-33-49.csv")

# Extraer las variables
X_data = df["Valor"].values

# Obtener detalles del modelo
input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

# Inicializar la ventana FIFO vacía
ventana_fifo = []
predicciones_fifo = []  # Vector FIFO para las estimaciones

Ingreso_real_time = []
Predecir_real_time = []

valor_predicion_pasado = 0
valor_predicion_actual = 0
contador = 0

# Iterar sobre los datos en tiempo real
for i in range(len(X_data)):
    nuevo_dato = X_data[i]
    nuevo_dato = min(nuevo_dato, 100)  # Saturar la entrada en 50
    
    if len(predicciones_fifo) == 20:
        predicciones_fifo.pop(0)

    if len(ventana_fifo) == 20:
        ventana_fifo.pop(0)  # Eliminar el dato más antiguo

    ventana_fifo.append(nuevo_dato)  # Ingresar el nuevo dato en la lista
    media_actual = sum(ventana_fifo) / len(ventana_fifo)  # Calcular la media correctamente
    Ingreso_real_time.append(media_actual)


    if len(ventana_fifo) == 20:  # Esperar a tener 20 elementos antes de predecir
        # Preparar la entrada para el modelo con los 10 valores más recientes
        entrada_modelo = np.array([Ingreso_real_time[-10:]]).reshape(1, 10, 1).astype(np.uint8)
        interpreter.set_tensor(input_details[0]['index'], entrada_modelo)
        interpreter.invoke()  # Ejecutar el modelo
        prediccion = interpreter.get_tensor(output_details[0]['index'])[0, 0]
        prediccion = prediccion < 200

        predicciones_fifo.append(prediccion)
        
        valor_predicion_actual = (sum(predicciones_fifo) / len(predicciones_fifo))>0.45
        
        if( valor_predicion_pasado == 1 and valor_predicion_actual == 0):
            contador = contador +1
        
        valor_predicion_pasado = valor_predicion_actual 


        Predecir_real_time.append(valor_predicion_actual)
    else:
        predicciones_fifo.append(0)  # Mantener sincronización de índices


print(contador)

# Graficar los resultados
plt.figure(figsize=(12, 6))
#plt.plot(X_data, label="Sensor Valor", linestyle="--", alpha=0.6)
plt.plot(Ingreso_real_time, label="Sensor Valor Media", linestyle="--", alpha=0.6)
plt.plot(range(19, len(X_data)), Predecir_real_time, label="Predicciones", color='red')
plt.xlabel("Índice de Tiempo")
plt.ylabel("Valor")
plt.title("Evolución de datos y predicciones en tiempo real")
plt.legend()
plt.show()

# Predecir_real_time contendrá las predicciones realizadas en tiempo real