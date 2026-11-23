# libaec decoder

Decoder sources from [libaec release 1.1.6](https://github.com/Deutsches-Klimarechenzentrum/libaec/releases/tag/v1.1.6).
The upstream archive has SHA-512
`97f05f6c80c32a9378e7bf8698053c1ee57d852ba988df9f052f3ac66b2f698b7cf7a0ea490c3ee6a5f4b0decac75b5f4fa11ea7a80c477aae5ef28e0a8b9b08`.
The C sources and private headers are unchanged. The public header has its
version substituted and the local configuration selects the available decoder
intrinsic. Only the decoder is included in the plugin build.
