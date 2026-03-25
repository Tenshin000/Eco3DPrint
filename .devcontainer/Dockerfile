FROM contiker/contiki-ng:latest

RUN git clone --recurse-submodules https://github.com/contiki-ng/contiki-ng.git /home/user/contiki-ng

RUN apt update && apt install -y \
    python3 \
    python3-tk \
    python3-pil.imagetk \
    curl

COPY requirements.txt /home/user/requirements.txt
RUN pip3 install -r /home/user/requirements.txt && rm /home/user/requirements.txt

# Install Node.js and coap-cli
RUN curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash \
    && export NVM_DIR="/home/user/.nvm" \
    && [ -s "$NVM_DIR/nvm.sh" ] && \. "$NVM_DIR/nvm.sh" \
    && nvm install 22 \
    && npm install coap-cli -g