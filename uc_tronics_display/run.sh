#!/usr/bin/with-contenv bashio

bashio::log.info "Starting UCTRONICS RM0013 160x80 color display v1.3.0"

if [[ ! -e /dev/i2c-1 ]]; then
    bashio::log.fatal "I2C device /dev/i2c-1 was not found."
    exit 1
fi

cd /lcd_display/ || exit 1
bashio::log.info "Building the LCD display program"
make clean
if ! make; then
    bashio::log.fatal "The LCD display program failed to compile."
    exit 1
fi

display_args=()
if bashio::config.true 'show_home_assistant_logo'; then display_args+=("--logo"); fi
if bashio::config.true 'show_ip_address'; then display_args+=("--ip"); fi
if bashio::config.true 'show_cpu_usage'; then display_args+=("--cpu"); fi
if bashio::config.true 'show_ram_usage'; then display_args+=("--ram"); fi
if bashio::config.true 'show_disk_space'; then display_args+=("--disk"); fi

screen_duration="$(bashio::config 'screen_duration')"
transition_type="$(bashio::config 'transition_type')"
transition_steps="$(bashio::config 'transition_steps')"
transition_delay_ms="$(bashio::config 'transition_delay_ms')"

if [[ ${#display_args[@]} -eq 0 ]] && ! bashio::config.true 'show_entity_state'; then
    bashio::log.warning "No display screens are enabled. Falling back to the Home Assistant logo."
    display_args+=("--logo")
fi

host_ip_cidr="$(bashio::network.ipv4_address default 2>/dev/null | head -n 1 || true)"
host_ip="${host_ip_cidr%%/*}"
if [[ ! "${host_ip}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    host_ip="Unavailable"
fi

disk_total="$(bashio::host.disk_total 2>/dev/null || true)"
disk_used="$(bashio::host.disk_used 2>/dev/null || true)"
disk_percent=0
if [[ "${disk_total}" =~ ^[0-9]+([.][0-9]+)?$ ]] && [[ "${disk_used}" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    disk_percent="$(awk -v used="${disk_used}" -v total="${disk_total}" 'BEGIN { if (total > 0) { v=(used/total)*100; if(v<0)v=0; if(v>100)v=100; printf "%.0f",v } else print 0 }')"
fi

entity_file="/tmp/uctronics_entity_state"
entity_updater_pid=""

cleanup() {
    if [[ -n "${entity_updater_pid}" ]]; then kill "${entity_updater_pid}" 2>/dev/null || true; fi
}
trap cleanup EXIT TERM INT

if bashio::config.true 'show_entity_state'; then
    entity_id="$(bashio::config 'entity_id')"
    entity_label="$(bashio::config 'entity_label')"
    entity_attribute="$(bashio::config 'entity_attribute')"
    entity_refresh="$(bashio::config 'entity_refresh_interval')"
    display_args+=("--entity" "--entity-file" "${entity_file}")

    update_entity_state() {
        local response value label unit temporary
        temporary="${entity_file}.tmp"
        response="$(curl --silent --show-error --fail \
            -H "Authorization: Bearer ${SUPERVISOR_TOKEN}" \
            -H "Content-Type: application/json" \
            "http://supervisor/core/api/states/${entity_id}" 2>/dev/null || true)"

        if [[ -z "${response}" ]]; then
            printf '%s
%s
%s
' "${entity_label:-ENTITY}" "Unavailable" "" > "${temporary}"
            mv "${temporary}" "${entity_file}"
            return
        fi

        label="${entity_label}"
        if [[ -z "${label}" ]]; then
            label="$(printf '%s' "${response}" | jq -r '.attributes.friendly_name // .entity_id // "ENTITY"')"
        fi

        if [[ -n "${entity_attribute}" ]]; then
            value="$(printf '%s' "${response}" | jq -r --arg key "${entity_attribute}" '.attributes[$key] // "Unavailable"')"
            unit=""
        else
            value="$(printf '%s' "${response}" | jq -r '.state // "Unavailable"')"
            unit="$(printf '%s' "${response}" | jq -r '.attributes.unit_of_measurement // ""')"
        fi

        case "${value}" in
            on) value="ON" ;;
            off) value="OFF" ;;
            unknown|unavailable|null|"") value="Unavailable" ;;
        esac

        label="${label//$'\n'/ }"; value="${value//$'\n'/ }"; unit="${unit//$'\n'/ }"
        printf '%s
%s
%s
' "${label}" "${value}" "${unit}" > "${temporary}"
        mv "${temporary}" "${entity_file}"
    }

    update_entity_state
    (
        while true; do
            sleep "${entity_refresh}"
            update_entity_state
        done
    ) &
    entity_updater_pid=$!
fi

bashio::log.info "Enabled display arguments: ${display_args[*]}"
bashio::log.info "Transition: ${transition_type} (${transition_steps} steps, ${transition_delay_ms} ms)"

exec ./display \
    "${display_args[@]}" \
    --duration "${screen_duration}" \
    --host-ip "${host_ip}" \
    --disk-percent "${disk_percent}" \
    --transition "${transition_type}" \
    --transition-steps "${transition_steps}" \
    --transition-delay-ms "${transition_delay_ms}"
