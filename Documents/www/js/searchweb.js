// SPDX-License-Identifier: CC-BY-SA-4.0

// Search functionality for Valkyrie OS website

// Define searchable pages and their content
const searchablePages = [
    {
        title: "Home",
        url: "index.html",
        description: "Overview of Valkyrie OS - A modern operating system with integrated Java Virtual Machine",
        keywords: ["home", "overview", "valkyrie", "operating system", "x86", "java", "jvm"]
    },
    {
        title: "About",
        url: "about.html",
        description: "Information about the Valkyrie OS project and its development",
        keywords: ["about", "information", "history", "project", "development"]
    },
    {
        title: "Documentation",
        url: "docs.html",
        description: "Complete documentation covering kernel architecture, bootloader, and development guides",
        keywords: ["documentation", "docs", "guide", "tutorial", "manual", "help", "kernel", "bootloader"]
    },
    {
        title: "Downloads",
        url: "downloads.html",
        description: "Download pre-built disk images of Valkyrie OS",
        keywords: ["download", "downloads", "release", "image", "floppy", "disk", "iso"]
    }
];

// Display search results
function displayResults(results, query) {
    let resultsContainer = document.getElementById('search-results');
    const searchBox = document.querySelector('.website-sidebar-box:has(.website-search-form)');
    
    // Create results container if it doesn't exist
    if (!resultsContainer) {
        resultsContainer = document.createElement('div');
        resultsContainer.id = 'search-results';
        resultsContainer.className = 'website-search-results';
        searchBox.appendChild(resultsContainer);
    }
    
    // Clear previous results
    resultsContainer.innerHTML = '';
    
    if (results.length === 0) {
        resultsContainer.innerHTML = `<div class="website-search-result-item" style="color: #666;">No results found for "<strong>${query}</strong>"</div>`;
    } else {
        results.forEach(result => {
            const snippet = result.page.description.substring(0, 80) + (result.page.description.length > 80 ? '...' : '');
            
            resultsContainer.innerHTML += `
                <div class="website-search-result-item">
                    <a href="${result.page.url}" class="website-search-result-link">${result.page.title}</a>
                    <div class="website-search-result-snippet">${snippet}</div>
                </div>
            `;
        });
    }
    
    resultsContainer.style.display = 'block';
}

// Search function
function searchWebsite(query) {
    const searchTerm = query.toLowerCase().trim();
    
    if (!searchTerm) {
        alert("Please enter a search term");
        return false;
    }
    
    let results = [];
    
    // Search through pages
    searchablePages.forEach(page => {
        // Check if any keyword matches the search term
        const matchedKeywords = page.keywords.filter(keyword => 
            keyword.includes(searchTerm) || searchTerm.includes(keyword)
        );
        
        // Also check title and description
        const titleMatch = page.title.toLowerCase().includes(searchTerm);
        const descriptionMatch = page.description.toLowerCase().includes(searchTerm);
        
        if (matchedKeywords.length > 0 || titleMatch || descriptionMatch) {
            results.push({
                page: page,
                relevance: matchedKeywords.length + (titleMatch ? 2 : 0) + (descriptionMatch ? 1 : 0)
            });
        }
    });
    
    // Sort by relevance
    results.sort((a, b) => b.relevance - a.relevance);
    
    // Display results
    displayResults(results, query);
    
    return false;
}

// Attach search form handler when page loads
document.addEventListener('DOMContentLoaded', function() {
    const searchForm = document.querySelector('.website-search-form');
    if (searchForm) {
        searchForm.addEventListener('submit', function(e) {
            e.preventDefault();
            const searchInput = document.querySelector('.website-search-input');
            searchWebsite(searchInput.value);
        });
    }
});
