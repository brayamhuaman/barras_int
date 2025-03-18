from flask import Flask, request, jsonify
from flask_cors import CORS  # Add this import
import tensorflow as tf
import numpy as np

# Ruta del modelo TFLite
TFLITE_MODEL_PATH = "Entrenamiento/NetSaturada50cm/modelo_5_loss-0_0954_acc-0_9800.tflite"

# 📌 Cargar modelo TensorFlow Lite
class ModeloTFLite:
    def __init__(self, model_path):
        self.interpreter = tf.lite.Interpreter(model_path=model_path)
        self.interpreter.allocate_tensors()
        self.input_details = self.interpreter.get_input_details()
        self.output_details = self.interpreter.get_output_details()

    def predecir(self, input_data):
        """ Ejecuta la inferencia en el modelo """
        input_array = np.array([input_data], dtype=np.uint8).reshape(1, 10, 1)  # Ajustar dimensiones
        self.interpreter.set_tensor(self.input_details[0]['index'], input_array)
        self.interpreter.invoke()
        output_data = self.interpreter.get_tensor(self.output_details[0]['index'])
        return output_data[0].tolist()

# 📌 Crear instancia del modelo
modelo = ModeloTFLite(TFLITE_MODEL_PATH)

# 📌 Crear API con Flask
app = Flask(__name__)
CORS(app)
@app.route('/predict/post', methods=['POST'])
def predict():
    try:
        data = request.get_json()
        input_data = np.array(data["inputs"]).astype(np.uint8)  # Convertir a array NumPy

        # Validar que el usuario envía 10 valores
        if len(input_data) != 10:
            return jsonify({"error": "Se requieren exactamente 10 valores como entrada"}), 400

        # 📌 Ejecutar modelo
        resultado = modelo.predecir(input_data)
        return jsonify({"prediction": resultado})

    except Exception as e:
        return jsonify({"error": str(e)}), 500

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=8080)  # Replit uses port 8080 by default
