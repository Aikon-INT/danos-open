# DANOS-Open adapter variables for the i-danos/tests suites.
# The live VM runs mgrd; QEMU hostfwd exposes gNMI (:59200) and
# metrics (:59201) on the host loopback.

GNMI_ADDR = "172.17.0.1:59200"
METRICS_URL = "http://172.17.0.1:59201/metrics"
GNMIC = "/usr/local/bin/gnmic"
