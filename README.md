# Ferraris-Power-Reader
Code for Arduino / ESP micro controller to read the current power consumption of your home / appartment by counting the turns of an old ferraris counter and measuring the consumption. Its value will be transmitted using MQTT to your smart home server. Its sensitivity and thresholds are adjusted by the solution in real time and shall fit almost every environment.

Requires an Arduino or ESP micro controller with an TCRT5000 or similar infra red sensor capable of transmitting its reading using the analog in pin of the ESP / Arduino. 

This counter does not depend on absolute values with fixed triggers like most other solutions you can find around the web. An sliding window will be used realized as a ring buffer to calculate the harmonized median value of the last 200 samples. If 3 or more samples are above a certain threshold, the counter will handles this as a rising edge and a single turn of the ferraris sensor disc. The time between the last rising adge and the current edge will be calculated and transmitted to a MQTT Broker. Measured values above a certain threshold will be discarded and handled as outliers to minimize errors.
In field usage, this solution proved to be very stable and multiple error avoidance hurdles lead to a stable solution with minimal maintenance cost and almost no outliers.

USAGE:
- Adjust the commented variables in this file. 
- Upload the code to your device of choice. 
- Enjoy

!!!!!!!!!!ATTENTION!!!!!!!!!!
This code is designed to quickly develope a running and stable solution. However, this comes with the cost of less readability and almost all principles of clean, straight and reusable code are violated as f***. 

WARNING: This product can expose you to abhorrent code, which is known to the State of California to cause eye cancer and tremendous headache. For more information, go to https://www.reddit.com/r/ProgrammerHumor/.
