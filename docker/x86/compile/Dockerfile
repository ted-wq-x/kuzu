FROM 172.16.1.99/tools/centos8/gitlab-builder:jdk8-dockerindocker

RUN rm -f /etc/yum.repos.d/* && \
    wget -P /etc/yum.repos.d http://172.18.136.72/config/transwarp.repo && \
    yum install --disablerepo=* --enablerepo=appstream,baseos,epel -y gdb libunwind libunwind-devel \
    cyrus-sasl-devel cyrus-sasl-gssapi cyrus-sasl-plain krb5-server krb5-workstation openssl-devel \
    && yum clean all

RUN wget http://172.18.132.68:18080/patch/software/OpenJDK8U-jdk_x64_linux_8u312b07.tar.gz \
    && tar -zxf OpenJDK8U-jdk_x64_linux_8u312b07.tar.gz -C /usr/java \
    && /usr/java/openjdk-8u312-b07/bin/keytool -noprompt -import -trustcacerts -alias transwarp_root \
    -file /srv/cacerts/ca.crt -keystore /usr/java/openjdk-8u312-b07/jre/lib/security/cacerts -storepass changeit \
    && rm -f OpenJDK8U-jdk_x64_linux_8u312b07.tar.gz

#upgrate python
# yum update -y && \
RUN yum install -y python39 gcc-c++ cmake && \
    rm -f /usr/local/bin/python3 && \
    ln -s  /usr/bin/python3.9 /usr/local/bin/python3

ENV JAVA8_312_HOME /usr/java/openjdk-8u312-b07

#upgrate gcc
RUN yum install -y gcc-toolset-11 && \
    echo "source /opt/rh/gcc-toolset-11/enable" >> /etc/bashrc && \
    source /etc/bashrc