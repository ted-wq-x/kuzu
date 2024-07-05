#!/usr/bin/env bash

function build_booster
{
    set -e

    echo "modify booster base image"
    DEFAULT_BASE_IMAGE="172.16.1.99.*linac.*"
    #component name of base image
    BASE_IMAGE_COMPONENT="linac"

    #expected base image should be running-time gernerated by repo,build,component,tag
    EXPECTED_BASE_IMAGE="${DOCKER_REPO_URL}/${BASE_IMAGE_BUILDER}/${BASE_IMAGE_COMPONENT}:${BASE_IMAGE_TAG}"
    #replace default base image in Dockerfile with new base image
    echo $EXPECTED_BASE_IMGE
    sed -i 's|'${DEFAULT_BASE_IMAGE}'|'${EXPECTED_BASE_IMAGE}'|g' docker/x86/builder/Dockerfile

    source /opt/rh/gcc-toolset-11/enable

    make test NUM_THREADS=$(nproc)

    make booster NUM_THREADS=$(nproc)

    docker build --network=host -t ${IMAGE_NAME} -f docker/x86/builder/Dockerfile .

}

build_booster   
