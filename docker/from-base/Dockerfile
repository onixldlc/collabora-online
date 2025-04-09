FROM onixldlc/collabora-online:base AS base
FROM ubuntu:24.04

# refresh repos otherwise installations later may fail
# install LibreOffice run-time dependencies
# install adduser, findutils, openssl and cpio that we need later
# install tzdata to accept the TZ environment variable
# install an editor
# tdf#117557 - Add CJK Fonts to Collabora Online Docker Image
RUN apt-get update \
    && apt-get -y install libpng16-16 fontconfig adduser cpio tzdata \
    findutils nano \
    libcap2-bin openssl openssh-client \
    libxcb-shm0 libxcb-render0 libxrender1 libxext6 \
    fonts-wqy-zenhei fonts-wqy-microhei fonts-droid-fallback \
    fonts-noto-cjk ca-certificates \
    && rm -rf /var/lib/apt/lists/*
# copy freshly built LOKit and Collabora Online
COPY --from=base /app/instdir /

# copy the shell script which can start Collabora Online (coolwsd)
COPY ./start-collabora-online.sh /start-collabora-online.sh

# set up Collabora Online (normally done by postinstall script of package)
# Fix permissions
RUN setcap cap_fowner,cap_chown,cap_sys_chroot=ep /usr/bin/coolforkit-caps && \
    setcap cap_sys_admin=ep /usr/bin/coolmount && \
    adduser --quiet --system --group --home /opt/cool cool && \
    rm -rf /opt/cool && \
    mkdir -p /opt/cool/child-roots /opt/cool/cache && \
    coolwsd-systemplate-setup /opt/cool/systemplate /opt/lokit >/dev/null 2>&1 && \
    touch /var/log/coolwsd.log && \
    chown cool:cool /var/log/coolwsd.log && \
    chown -R cool:cool /opt/ && \
    chown -R cool:cool /etc/coolwsd

EXPOSE 9980

# switch to cool user (use numeric user id to be compatible with Kubernetes Pod Security Policies)
USER 100

ENTRYPOINT ["/start-collabora-online.sh"]