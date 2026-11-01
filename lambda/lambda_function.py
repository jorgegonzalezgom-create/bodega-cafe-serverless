# lambda_function.py: API del dashboard de la bodega de cafe
#   GET  /lecturas      -> lecturas en JSON (para las graficas)
#   GET  /lecturas/csv  -> lecturas en CSV (para descargar)
#   POST /led           -> envia ON/OFF al ESP32 por MQTT (actuador)

import json, time, base64
from decimal import Decimal
import boto3
from boto3.dynamodb.conditions import Key

TABLA = "bodega-lecturas"
DISPOSITIVO = "bodega-esp32"
IOT_ENDPOINT = "https://ajmlhdd4q23bu-ats.iot.us-east-1.amazonaws.com"

tabla = boto3.resource("dynamodb").Table(TABLA)                 # conexion a DynamoDB
iot = boto3.client("iot-data", endpoint_url=IOT_ENDPOINT)       # conexion al broker de IoT Core

# Permite que el dashboard (en S3, otro dominio) llame a esta API
CORS = {"Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Headers": "Content-Type",
        "Access-Control-Allow-Methods": "GET,POST,OPTIONS"}


def respuesta(codigo, cuerpo, tipo="application/json", extra=None):
    """Arma la respuesta HTTP que entiende API Gateway."""
    headers = {**CORS, "Content-Type": tipo}
    if extra:
        headers.update(extra)
    return {"statusCode": codigo, "headers": headers, "body": cuerpo}


def leer(horas):
    """Lee de DynamoDB las lecturas de las ultimas `horas`, ordenadas por tiempo."""
    desde = int((time.time() - horas * 3600) * 1000)            # ts en milisegundos
    consulta = {"KeyConditionExpression":
                Key("dispositivo").eq(DISPOSITIVO) & Key("ts").gte(desde)}
    items = []
    while True:                                                 # DynamoDB entrega por paginas
        r = tabla.query(**consulta)
        items += r["Items"]
        if "LastEvaluatedKey" not in r:
            break
        consulta["ExclusiveStartKey"] = r["LastEvaluatedKey"]
    # DynamoDB guarda los numeros como Decimal: se pasan a float para JSON
    return [{k: (float(v) if isinstance(v, Decimal) else v) for k, v in it.items()}
            for it in items]


def lambda_handler(event, context):
    metodo = event.get("requestContext", {}).get("http", {}).get("method", "GET")
    ruta = event.get("rawPath", "/")
    params = event.get("queryStringParameters") or {}

    if metodo == "OPTIONS":                                     # pregunta previa del navegador (CORS)
        return respuesta(204, "")

    # ---- 1. Lecturas en JSON ----
    if metodo == "GET" and ruta.endswith("/lecturas"):
        horas = float(params.get("horas", 1))
        return respuesta(200, json.dumps(leer(horas)))

    # ---- 2. Lecturas en CSV ----
    if metodo == "GET" and ruta.endswith("/csv"):
        horas = float(params.get("horas", 24))
        lineas = ["fecha,dispositivo,temperatura,humedad,estado,led"]
        for f in leer(horas):
            # ts (UTC, ms) -> hora de Colombia (UTC-5)
            fecha = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(f["ts"] / 1000 - 5 * 3600))
            lineas.append(f'{fecha},{f.get("dispositivo")},{f.get("temperatura")},'
                          f'{f.get("humedad")},{f.get("estado")},{f.get("led")}')
        return respuesta(200, "\n".join(lineas), "text/csv",
                         {"Content-Disposition": 'attachment; filename="lecturas_bodega.csv"'})

    # ---- 3. Actuador: prender/apagar el LED ----
    if metodo == "POST" and ruta.endswith("/led"):
        cuerpo = event.get("body") or "{}"
        if event.get("isBase64Encoded"):
            cuerpo = base64.b64decode(cuerpo).decode()
        estado = str(json.loads(cuerpo).get("estado", "")).upper()
        if estado not in ("ON", "OFF"):
            return respuesta(400, json.dumps({"error": "estado debe ser ON u OFF"}))
        iot.publish(topic="bodega1/led", qos=1, payload=estado)  # MQTT hacia el ESP32
        return respuesta(200, json.dumps({"ok": True, "led": estado}))

    return respuesta(404, json.dumps({"error": "ruta no encontrada"}))
