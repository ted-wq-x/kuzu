FROM 172.16.1.99/postcommit/linac:develop

LABEL maintainer=jingjing.han@transwarp.io

RUN yum install -y libatomic && yum clean all

ADD build/release/stellar_booster/libstellar_booster.so /usr/lib/inceptor/lib/native/