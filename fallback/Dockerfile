# Offline fallback: the lab environment with the lab files baked in.
# Built by fallback/build_offline_image.sh, distributed on USB.
FROM pqc-ota-lab-env
COPY --chown=lab:lab . /home/lab/lab
WORKDIR /home/lab/lab
RUN cp -n device/factory.bin device/flash_image.bin
CMD ["bash"]
