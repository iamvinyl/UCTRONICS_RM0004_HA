FROM ghcr.io/home-assistant/base:latest
ENV LANG C.UTF-8

RUN set -xe \
    && apk add --no-cache gcc build-base git i2c-tools linux-headers cmake curl jq

COPY /lcd_display/* /lcd_display/
COPY run.sh /
RUN chmod a+x /run.sh
CMD [ "/run.sh" ]
