#ifdef LW_WITH_OIDN

#include <OpenImageDenoise/oidn.hpp>
#include <lightwave.hpp>

namespace lightwave {

class Denoising : public Postprocess {

private:
    ref<Image> m_normals;
    ref<Image> m_albedo;

public:
    Denoising(const Properties &properties) : Postprocess(properties) {
        m_albedo  = properties.get<Image>("albedo");
        m_normals = properties.get<Image>("normals");
    }

    void execute() override {
        float height;
        float width;

        width  = m_input->resolution().x();
        height = m_input->resolution().y();

        m_output->initialize(m_input->resolution());

        // // create a new Open Image Denoise
        // oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
        // device.commit();

        // create an OpenImageDenoise device
        oidn::DeviceRef device = oidn::newDevice();
        device.commit();

        oidn::BufferRef colorBuf =
            device.newBuffer(width * height * 3 * sizeof(float));

        // reate a denoising filter
        oidn::FilterRef filter = device.newFilter("RT");

        // Prepare input buffers
        filter.setImage(
            "color", m_input->data(), oidn::Format::Float3, width, height);
        filter.setImage(
            "normal", m_normals->data(), oidn::Format::Float3, width, height);
        filter.setImage(
            "albedo", m_albedo->data(), oidn::Format::Float3, width, height);
        filter.setImage(
            "output", m_output->data(), oidn::Format::Float3, width, height);

        filter.commit();

        filter.execute(); // run the denoising filter on the input image

        if (device.getError() != oidn::Error::None)
            std::cout << "Error: " << std::endl;
        // upper code so we get the error message if there is any

        m_output->save(); // save output image
    }

    // std::string toString() const override { return "Denosing[\n", "]"; }
    std::string toString() const override {
        return tfm::format(
            "Denoising[\n"
            " m_input = %s,\n"
            " m_output = %s,\n"
            " m_normals = %s,\n"
            " m_albedo = %s,\n"
            "]",
            indent(m_input),
            indent(m_output),
            indent(m_normals),
            indent(m_albedo));
        // indent(m_output));
    }
};
} // namespace lightwave

REGISTER_POSTPROCESS(Denoising, "denoising")
#endif
