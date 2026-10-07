# hpDIRC analysis environment: ROOT + sPHENIX online_distribution (Event library) + Jupyter
FROM docker.io/rootproject/root:6.32.02-ubuntu24.04

# ePIC is the actively developed branch (master is stale); it has the current V1742 decoder
ARG ONLINE_DISTRIBUTION_REF=ePIC

# (APT::Sandbox::User=root only matters for rootless podman without subuids; harmless elsewhere)
RUN apt-get -o APT::Sandbox::User=root update \
    && DEBIAN_FRONTEND=noninteractive apt-get -o APT::Sandbox::User=root install -y --no-install-recommends \
        git autoconf automake libtool make g++ pkg-config \
        zlib1g-dev liblzo2-dev libbz2-dev libboost-dev \
        python3-numpy python3-matplotlib python3-pandas python3-scipy python3-ipykernel python-is-python3 \
    && rm -rf /var/lib/apt/lists/*

ENV ONLINE_MAIN=/opt/online
ENV PATH=${ONLINE_MAIN}/bin:${PATH}
ENV LD_LIBRARY_PATH=${ONLINE_MAIN}/lib:${LD_LIBRARY_PATH}
ENV ROOT_INCLUDE_PATH=${ONLINE_MAIN}/include:${ONLINE_MAIN}/include/Event

RUN git clone https://github.com/sPHENIX-Collaboration/online_distribution.git /tmp/od \
    && git -C /tmp/od checkout ${ONLINE_DISTRIBUTION_REF} \
    && git -C /tmp/od log -1 --format='online_distribution %H %cd' > /tmp/od/VERSION

RUN mkdir -p /tmp/build/eventlibraries && cd /tmp/build/eventlibraries \
    && /tmp/od/eventlibraries/autogen.sh --prefix=${ONLINE_MAIN} \
    && make -j"$(nproc)" install

RUN mkdir -p /tmp/build/pmonitor && cd /tmp/build/pmonitor \
    && /tmp/od/pmonitor/autogen.sh --prefix=${ONLINE_MAIN} \
    && make -j"$(nproc)" install \
    && cp /tmp/od/VERSION ${ONLINE_MAIN}/VERSION \
    && rm -rf /tmp/od /tmp/build

# A Jupyter kernel with the ROOT / Event-library environment baked in, so notebooks work
# without relying on VS Code's Python-environment activation.
RUN python3 -m ipykernel install --prefix /usr/local --name hpdirc --display-name "hpDIRC (Python 3 + ROOT)" \
    && python3 -c "import json; p='/usr/local/share/jupyter/kernels/hpdirc/kernel.json'; k=json.load(open(p)); \
k['env']={'ROOTSYS':'/opt/root','PYTHONPATH':'/opt/root/lib','CLING_STANDARD_PCH':'none', \
'ONLINE_MAIN':'/opt/online','LD_LIBRARY_PATH':'/opt/online/lib', \
'ROOT_INCLUDE_PATH':'/opt/online/include:/opt/online/include/Event'}; json.dump(k,open(p,'w'),indent=1)"
