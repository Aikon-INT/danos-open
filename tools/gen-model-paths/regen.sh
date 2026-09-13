#!/bin/bash
# Regenerate the model path table after editing model_paths.json
set -e
cd "$(dirname "$0")"
./gen-model-paths model_paths.json ../../danos-mgmt/src/gnmi/model_paths_gen.inc
