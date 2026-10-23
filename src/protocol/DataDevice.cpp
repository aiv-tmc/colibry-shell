// Implementation of the clipboard data device protocols.

#include "DataDevice.hpp"

#include "Display.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include <wayland-server-protocol.h>

namespace Colibry {

    CDataDeviceManager& CDataDeviceManager::get() {
        static CDataDeviceManager s_instance;
        return s_instance;
    }

    bool CDataDeviceManager::init(CDisplay* display) {
        m_display = display;

        m_global = wl_global_create(display->waylandDisplay(), &wl_data_device_manager_interface, 3, this, &CDataDeviceManager::bind);
        if (!m_global) {
            std::fprintf(stderr, "[colibry] failed to create wl_data_device_manager global\n");
            return false;
        }

        return true;
    }

    void CDataDeviceManager::bind(wl_client* client, void* data, uint32_t version, uint32_t id) {
        const auto resource = wl_resource_create(client, &wl_data_device_manager_interface, version, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_data_device_manager_interface IMPLEMENTATION = {
            .create_data_source = createDataSource,
            .get_data_device    = getDataDevice,
        };

        wl_resource_set_implementation(resource, &IMPLEMENTATION, data, nullptr);
    }

    void CDataDeviceManager::createDataSource(wl_client* client, wl_resource* /*resource*/, uint32_t id) {
        auto& self = get();

        const auto resource = wl_resource_create(client, &wl_data_source_interface, 1, id);
        if (!resource) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_data_source_interface IMPLEMENTATION = {
            .offer   = sourceOffer,
            .destroy = sourceDestroy,
        };

        self.m_sources.push_back(SDataSource{.resource = resource, .mimes = {}});
        SDataSource* source = &self.m_sources.back();

        // The source lives as long as its resource.
        wl_resource_set_implementation(resource, &IMPLEMENTATION, source, [](wl_resource* res) {
            auto& self          = get();
            SDataSource* source = static_cast<SDataSource*>(wl_resource_get_user_data(res));

            // Losing the selection source clears the selection for
            // everyone.
            if (self.m_selection == source) {
                self.m_selection = nullptr;
                self.destroySelectionOffers();

                for (const auto device : self.m_devices)
                    wl_data_device_send_selection(device, nullptr);
            }

            for (auto it = self.m_sources.begin(); it != self.m_sources.end(); ++it) {
                if (&*it == source) {
                    self.m_sources.erase(it);
                    break;
                }
            }
        });
    }

    void CDataDeviceManager::sourceOffer(wl_client* /*client*/, wl_resource* resource, const char* mime) {
        auto* source = static_cast<SDataSource*>(wl_resource_get_user_data(resource));
        if (source && mime && *mime)
            source->mimes.emplace_back(mime);
    }

    void CDataDeviceManager::sourceDestroy(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CDataDeviceManager::getDataDevice(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* /*seat*/) {
        auto& self = get();

        // The device is capped at version 2: version 3 belongs to the
        // manager and drag-and-drop additions are not used.
        const auto device = wl_resource_create(client, &wl_data_device_interface, std::min(wl_resource_get_version(resource), 2), id);
        if (!device) {
            wl_client_post_no_memory(client);
            return;
        }

        static const struct wl_data_device_interface IMPLEMENTATION = {
            .start_drag    = startDrag,
            .set_selection = setSelection,
            .release       = release,
        };

        wl_resource_set_implementation(device, &IMPLEMENTATION, &self, [](wl_resource* res) {
            auto& self = get();

            // Drops the offers handed to this device.
            std::vector<wl_resource*> doomed;
            for (const auto& offer : self.m_offers) {
                if (offer.device == res)
                    doomed.push_back(offer.resource);
            }
            for (const auto offer : doomed)
                wl_resource_destroy(offer);

            std::erase(self.m_devices, res);
        });

        self.m_devices.push_back(device);
    }

    void CDataDeviceManager::startDrag(wl_client*, wl_resource*, wl_resource* source, wl_resource*, wl_resource*, uint32_t) {
        // Drag and drop is not implemented; a source passed here is
        // cancelled so the client can clean up.
        if (source)
            wl_data_source_send_cancelled(source);
    }

    void CDataDeviceManager::setSelection(wl_client* client, wl_resource* /*resource*/, wl_resource* sourceResource, uint32_t) {
        auto& self = get();

        SDataSource* source = nullptr;
        if (sourceResource) {
            source = static_cast<SDataSource*>(wl_resource_get_user_data(sourceResource));

            // A client may only publish its own source.
            if (!source || wl_resource_get_client(source->resource) != client)
                return;
        }

        self.publishSelection(source);
    }

    void CDataDeviceManager::release(wl_client* /*client*/, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

    void CDataDeviceManager::publishSelection(SDataSource* source) {
        // Cancels the previous source.
        if (m_selection && m_selection != source)
            wl_data_source_send_cancelled(m_selection->resource);
        m_selection = source;

        destroySelectionOffers();

        // Every device client receives an offer describing the selection.
        for (const auto device : m_devices) {
            if (!source) {
                wl_data_device_send_selection(device, nullptr);
                continue;
            }

            const auto client = wl_resource_get_client(device);

            // The source owner's own device gets no offer.
            if (wl_resource_get_client(source->resource) == client) {
                wl_data_device_send_selection(device, nullptr);
                continue;
            }

            const auto offer = wl_resource_create(client, &wl_data_offer_interface, wl_resource_get_version(device), 0);
            if (!offer)
                continue;

            static const struct wl_data_offer_interface OFFER_IMPLEMENTATION = {
                .accept      = offerAccept,
                .receive     = offerReceive,
                .destroy     = offerDestroy,
                .finish      = offerFinish,
                .set_actions = offerSetActions,
            };

            m_offers.push_back(SDataOffer{.resource = offer, .source = source, .device = device});
            SDataOffer* offerData = &m_offers.back();

            wl_resource_set_implementation(offer, &OFFER_IMPLEMENTATION, offerData, [](wl_resource* res) {
                auto& self      = get();
                SDataOffer* data = static_cast<SDataOffer*>(wl_resource_get_user_data(res));
                for (auto it = self.m_offers.begin(); it != self.m_offers.end(); ++it) {
                    if (&*it == data) {
                        self.m_offers.erase(it);
                        break;
                    }
                }
            });

            wl_data_device_send_data_offer(device, offer);
            for (const auto& mime : source->mimes)
                wl_data_offer_send_offer(offer, mime.c_str());
            wl_data_device_send_selection(device, offer);
        }

        if (source)
            std::fprintf(stderr, "[colibry] selection set (%zu mime types)\n", source->mimes.size());
    }

    void CDataDeviceManager::destroySelectionOffers() {
        // Destroying a resource removes its offer from the list inside the
        // destroy callback, so collect the targets first.
        std::vector<wl_resource*> doomed;
        doomed.reserve(m_offers.size());
        for (const auto& offer : m_offers)
            doomed.push_back(offer.resource);

        for (const auto offer : doomed)
            wl_resource_destroy(offer);
    }

    void CDataDeviceManager::offerAccept(wl_client*, wl_resource*, uint32_t, const char*) {
        // The accepted mime is advisory for the source's rendering; with no
        // drag and drop there is nothing to forward.
    }

    void CDataDeviceManager::offerReceive(wl_client*, wl_resource* resource, const char* mime, int32_t fd) {
        auto* offer = static_cast<SDataOffer*>(wl_resource_get_user_data(resource));
        if (!offer || !offer->source) {
            close(fd);
            return;
        }

        // The fd is the pipe the destination client created for reading;
        // the source writes into it upon the send event.
        wl_data_source_send_send(offer->source->resource, mime, fd);
        close(fd);
    }

    void CDataDeviceManager::offerFinish(wl_client*, wl_resource*) {
        // Transfer completion marker of the drag and drop protocol.
    }

    void CDataDeviceManager::offerSetActions(wl_client*, wl_resource*, uint32_t, uint32_t) {
        // Drag and drop action negotiation; unused without dnd.
    }

    void CDataDeviceManager::offerDestroy(wl_client*, wl_resource* resource) {
        wl_resource_destroy(resource);
    }

}
