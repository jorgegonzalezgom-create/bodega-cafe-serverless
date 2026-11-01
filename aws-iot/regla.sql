-- Regla de IoT "bodega_guardar_lecturas"
-- Toma cada mensaje de bodega1/lecturas, le agrega la hora de llegada (ms)
-- y lo guarda con la accion DynamoDBv2 en la tabla bodega-lecturas (rol LabRole).
SELECT *, timestamp() AS ts FROM 'bodega1/lecturas'
