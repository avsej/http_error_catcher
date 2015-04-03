# Usage

Put this location in your nginx config

    error_page 502 503 504 @write_riemann;

    location @write_riemann {
        proxy_set_header  X-Status-Code $status;
        proxy_set_header  Host $http_host;
        proxy_set_header  Cookie "";
        proxy_set_header  Authorization "";
        proxy_redirect    off;
        proxy_pass        http://localhost:9000;
    }
