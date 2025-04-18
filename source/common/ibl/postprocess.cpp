#include "ibl/postprocess.hpp"

namespace our
{

    void PostProcess::init(const glm::ivec2 &windowSize, const nlohmann::json &config)
    {
        // Store the window size for later use
        this->windowSize = windowSize;

        postprocessFrameBuffer = 0;

        bloomEnabled = config.value("bloomEnabled", false);
        bloomIntensity = config.value("bloomIntensity", 1.0f);
        bloomIterations = config.value("bloomIterations", 10);
        bloomDirection = config.value("bloomDirection", BloomDirection::BOTH);
        tonemappingEnabled = config.value("tonemappingEnabled", false);
        gammaCorrectionFactor = config.value("gammaCorrectionFactor", 2.2f);
        bloomBrightnessCutoff = config.value("bloomBrightnessCutoff", 1.0f);

        // Create the bloom shader
        bloomShader = our::AssetLoader<our::ShaderProgram>::get("bloom");

        // TODO: (Req 11) Create a framebuffer
        glGenFramebuffers(1, &postprocessFrameBuffer);

        // TODO: (Req 11) Create a color and a depth texture and attach them to the framebuffer
        //  Hints: The color format can be (Red, Green, Blue and Alpha components with 8 bits for each channel).
        //  The depth format can be (Depth component with 24 bits).
        colorTarget = new Texture2D();
        colorTarget->bind();

        GLsizei levelsCnt = (GLsizei)glm::floor(glm::log2((float)glm::max(windowSize.x, windowSize.y))) + 1;
        glTexStorage2D(GL_TEXTURE_2D, levelsCnt, GL_RGBA8, windowSize.x, windowSize.y);

        depthTarget = new Texture2D();
        depthTarget->bind();

        glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, windowSize.x, windowSize.y);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postprocessFrameBuffer);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget->getOpenGLName(), 0);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTarget->getOpenGLName(), 0);

        // create bloom texture
        if (bloomEnabled){
            createBloom();
        }

        // allow to draw to both color attachments
        unsigned int colorAttachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, colorAttachments);

        // TODO: (Req 11) Unbind the framebuffer just to be safe
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

        //  // Create a vertex array to use for drawing the texture
        // ! WE NOW DO THIS IN THE FULLSCREEN QUAD CLASS
        //  glGenVertexArrays(1, &postProcessVertexArray);

        // Create a sampler to use for sampling the scene texture in the post processing shader
        Sampler *postprocessSampler = new Sampler();
        postprocessSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        postprocessSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        postprocessSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        postprocessSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Create the post processing shader
        ShaderProgram *postprocessShader = our::AssetLoader<our::ShaderProgram>::get("postprocess");

        // Create a post processing material
        postprocessMaterial = new TexturedMaterial();
        postprocessMaterial->shader = postprocessShader;
        postprocessMaterial->texture = colorTarget;
        postprocessMaterial->sampler = postprocessSampler;
        // The default options are fine but we don't need to interact with the depth buffer
        // so it is more performant to disable the depth mask
        postprocessMaterial->pipelineState.depthMask = false;

        // Initialize the fullscreen quad
        fullscreenQuad = new FullscreenQuad();
    }

    void PostProcess::createBloom()
    {
        // Initialize bloom buffers and framebuffer
        bloomBuffers[0] = new BloomFramebuffer(windowSize.x, windowSize.y);
        bloomBuffers[1] = new BloomFramebuffer(windowSize.x, windowSize.y);
        bloomBuffers[0]->init();
        bloomBuffers[1]->init();
        bloomFramebufferResult = 0;
        bloomColorTexture = 0;

        // Create a bloom texture
        glGenTextures(1, &bloomColorTexture);
        glBindTexture(GL_TEXTURE_2D, bloomColorTexture);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postprocessFrameBuffer);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, windowSize.x, windowSize.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, bloomColorTexture, 0);

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void PostProcess::destroy()
    {
        // Delete all objects related to post processing
        if (postprocessMaterial)
        {
            glDeleteFramebuffers(1, &postprocessFrameBuffer);
            delete colorTarget;
            delete depthTarget;
            delete postprocessMaterial->sampler;
            delete postprocessMaterial->shader;
            delete postprocessMaterial;
        }

        // Delete bloom buffers
        for (int i = 0; i < 2; ++i)
        {
            delete bloomBuffers[i];
        }
    }
    
    void PostProcess::bind()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, postprocessFrameBuffer);
        unsigned int colorAttachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, colorAttachments);
    }

    void PostProcess::unbind()
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    }

    void PostProcess::renderBloom()
    {

        glViewport(0, 0, windowSize.x, windowSize.y);

        // Define blur directions
        glm::vec2 blurDirectionX = glm::vec2(1.0f, 0.0f);
        glm::vec2 blurDirectionY = glm::vec2(0.0f, 1.0f);

        // Adjust blur directions based on bloomDirection
        if (bloomDirection == BloomDirection::HORIZONTAL)
        {
            blurDirectionY = blurDirectionX;
        }
        else if (bloomDirection == BloomDirection::VERTICAL)
        {
            blurDirectionX = blurDirectionY;
        }

        // use texture unit 0 for bloomColorTexture
        glActiveTexture(GL_TEXTURE0);
        // bind the bloom color texture
        glBindTexture(GL_TEXTURE_2D, bloomColorTexture);
        // generate mipmaps for the bloom color texture
        glGenerateMipmap(GL_TEXTURE_2D);

        bloomShader->use();
        bloomShader->set("inputColorTexture", 0);

        for (auto mipLevel = 0; mipLevel <= 2; mipLevel++)
        {
            bloomBuffers[0]->setMipLevel(mipLevel);
            bloomBuffers[1]->setMipLevel(mipLevel);

            // First pass (horizontal)
            bloomBuffers[0]->bind();
            glBindTexture(GL_TEXTURE_2D, bloomColorTexture);
            bloomShader->set("sampleMipLevel", mipLevel);
            bloomShader->set("blurDirection", blurDirectionX); // Always use X first
            fullscreenQuad->Draw();

            // Second pass (vertical) - applied to the result of horizontal
            bloomBuffers[1]->bind();
            glBindTexture(GL_TEXTURE_2D, bloomBuffers[0]->getColorTextureId());
            bloomShader->set("blurDirection", blurDirectionY); // Then use Y
            fullscreenQuad->Draw();

            // Now bloomBuffers[1] contains a proper circular blur

            // Additional iterations (apply more blur passes if needed)
            unsigned int bloomFramebuffer = 0;

            for (auto i = 1; i < bloomIterations; i++)
            {
                // Horizontal pass
                bloomBuffers[bloomFramebuffer]->bind();
                glBindTexture(GL_TEXTURE_2D, bloomBuffers[1]->getColorTextureId());
                bloomShader->set("blurDirection", blurDirectionX);
                fullscreenQuad->Draw();

                // Vertical pass
                bloomBuffers[1]->bind();
                glBindTexture(GL_TEXTURE_2D, bloomBuffers[bloomFramebuffer]->getColorTextureId());
                bloomShader->set("blurDirection", blurDirectionY);
                fullscreenQuad->Draw();
            }

            // The final result is always in bloomBuffers[1]
            bloomFramebufferResult = 1;
        }

        glBindTexture(GL_TEXTURE_2D, 0);
    }

    void PostProcess::renderPostProcess()
    {

        if (bloomEnabled){
            renderBloom();
        }

        // TODO: (Req 11) Return to the default framebuffer
        glViewport(0, 0, windowSize.x, windowSize.y);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        
        // TODO: (Req 11) Setup the postprocess material and draw the fullscreen triangle
        postprocessMaterial->setup();
        postprocessMaterial->shader->set("bloomEnabled", bloomEnabled);
        postprocessMaterial->shader->set("bloomIntensity", bloomIntensity);
        postprocessMaterial->shader->set("tonemappingEnabled", tonemappingEnabled);
        postprocessMaterial->shader->set("gammaCorrectionFactor", gammaCorrectionFactor);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, colorTarget->getOpenGLName());
        postprocessMaterial->shader->set("colorTexture", 0);

        if(bloomEnabled){
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, bloomBuffers[bloomFramebufferResult]->getColorTextureId());
            postprocessMaterial->shader->set("bloomTexture", 1);
        }
        
        // ! WE NOW DO THIS IN THE FULLSCREEN QUAD CLASS
        // glBindVertexArray(postProcessVertexArray);
        // glDrawArrays(GL_TRIANGLES, 0, 3);

        fullscreenQuad->Draw();
    }

}