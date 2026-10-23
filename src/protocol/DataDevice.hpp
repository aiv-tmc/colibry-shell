// Clipboard (data device) support for the Colibry compositor.
//
// CDataDeviceManager implements the core Wayland data-device protocols:
// clients advertise mime types through wl_data_source, publish the
// selection with wl_data_device.set_selection, and receive the selection
// as wl_data_offer objects. Transferring data pipes the destination
// client's file descriptor to the source client through the data_source
// send event. Drag and drop requests are not handled yet.

#pragma once

#include <wayland-server.h>

#include <list>
#include <string>
#include <vector>

namespace Colibry {

    class CDisplay;

    class CDataDeviceManager {
      public:
        static CDataDeviceManager& get();

        // Creates the wl_data_device_manager global on the display.
        bool init(CDisplay* display);

      private:
        CDataDeviceManager()  = default;
        ~CDataDeviceManager() = default;

        CDataDeviceManager(const CDataDeviceManager&)            = delete;
        CDataDeviceManager& operator=(const CDataDeviceManager&) = delete;

        // One advertised data source owned by a client.
        struct SDataSource {
            wl_resource*             resource;
            std::vector<std::string> mimes;
        };

        // One offer handed to a device client for the current selection.
        struct SDataOffer {
            wl_resource* resource;
            SDataSource* source;
            wl_resource* device;
        };

        static void bind(wl_client* client, void* data, uint32_t version, uint32_t id);

        static void createDataSource(wl_client* client, wl_resource* resource, uint32_t id);
        static void getDataDevice(wl_client* client, wl_resource* resource, uint32_t id, wl_resource* seat);

        // Source requests.
        static void sourceOffer(wl_client* client, wl_resource* resource, const char* mime);
        static void sourceDestroy(wl_client* client, wl_resource* resource);

        // Device requests.
        static void startDrag(wl_client* client, wl_resource* resource, wl_resource* source, wl_resource* origin, wl_resource* icon,
                              uint32_t serial);
        static void setSelection(wl_client* client, wl_resource* resource, wl_resource* source, uint32_t serial);
        static void release(wl_client* client, wl_resource* resource);

        // Offer requests.
        static void offerAccept(wl_client* client, wl_resource* resource, uint32_t serial, const char* mime);
        static void offerReceive(wl_client* client, wl_resource* resource, const char* mime, int32_t fd);
        static void offerFinish(wl_client* client, wl_resource* resource);
        static void offerSetActions(wl_client* client, wl_resource* resource, uint32_t dndActions, uint32_t preferredAction);
        static void offerDestroy(wl_client* client, wl_resource* resource);

        // Publishes the new selection (nullptr clears it): the old source is
        // cancelled, old offers destroyed, and every device client receives
        // fresh offers plus a selection event.
        void publishSelection(SDataSource* source);
        // Destroys all selection offers handed out to device clients.
        void destroySelectionOffers();

        CDisplay*  m_display = nullptr;
        wl_global* m_global  = nullptr;

        std::list<SDataSource> m_sources;
        std::list<SDataOffer>  m_offers;
        std::vector<wl_resource*> m_devices;

        SDataSource* m_selection = nullptr;
    };

}
