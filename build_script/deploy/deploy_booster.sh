#!/usr/bin/env bash

############################ ##########################
##      This is the template of build script         ##
#######################################################

function deploy_booster
## The function name should be the same as script name
{
    set -e

    cd stellar_booster

    export releaseStagingId="priv-transwarp-lib"
    export releaseRepoName="libs-release-local"
    export releaseRepoUrl="http://172.16.1.161:30033/repository/libs-release-local"
    export snapStagingId="priv-transwarp-snapshots"
    export snapRepoName="libs-snapshot"
    export snapRepoUrl="http://172.16.1.161:30033/repository/libs-snapshot-local"

    mvn  deploy -DskipTests -DdistMgmtStagingId=${releaseStagingId} \
		-DdistMgmtStagingName=${releaseRepoName} -DdistMgmtStagingUrl=${releaseRepoUrl} \
		-DdistMgmtSnapshotsId=${snapStagingId} -DdistMgmtSnapshotsName=${snapRepoName} \
		-DdistMgmtSnapshotsUrl=${snapRepoUrl}
    
    export public_releaseStagingId="public-transwarp-release"
    export public_releaseRepoName="libs-release-local"
    export public_releaseRepoUrl="http://172.16.1.168:8081/artifactory/libs-release-local"
    export public_snapStagingId="public-transwarp-snapshots"
    export public_snapRepoName="libs-snapshot"
    export public_snapRepoUrl="http://172.16.1.168:8081/artifactory/libs-snapshot-local"

    mv /root/.m2/settings.xml.pub /root/.m2/settings.xml

    mvn deploy -DskipTests -DdistMgmtStagingId=${public_releaseStagingId} \
        -DdistMgmtStagingName=${public_releaseRepoName} -DdistMgmtStagingUrl=${public_releaseRepoUrl} \
        -DdistMgmtSnapshotsId=${public_snapStagingId} -DdistMgmtSnapshotsName=${public_snapRepoName} \
        -DdistMgmtSnapshotsUrl=${public_snapRepoUrl}
    cd -
}
deploy_booster
