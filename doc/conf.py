# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only

# Sphinx configuration of the zspot documentation.
#
#   pip install -r doc/requirements.txt
#   sphinx-build -b html doc doc/_build/html

project = "zspot"
author = "Muhammad Waleed Badar"
copyright = "2026, Muhammad Waleed Badar"

extensions = [
    "sphinx.ext.githubpages",
]

exclude_patterns = ["_build", "requirements.txt"]

primary_domain = "c"
highlight_language = "none"

html_theme = "furo"
html_title = "zspot"
html_logo = "images/zspot-logo.svg"
html_favicon = "images/zspot-logo.svg"
html_static_path = []
html_theme_options = {
    "source_repository": "https://github.com/walidbadar/zspot",
    "source_branch": "main",
    "source_directory": "doc/",
}
