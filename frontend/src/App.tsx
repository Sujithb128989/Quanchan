import { BrowserRouter, Routes, Route } from 'react-router-dom';
import Layout from './components/Layout';
import HomePage from './pages/HomePage';
import BoardPage from './pages/BoardPage';
import CatalogPage from './pages/CatalogPage';
import ArchivePage from './pages/ArchivePage';
import ThreadPage from './pages/ThreadPage';
import DirectoryPage from './pages/DirectoryPage';
import DirectMessagesPage from './pages/DirectMessagesPage';
import AboutPage from './pages/AboutPage';
import ContactPage from './pages/ContactPage';
import NotificationsPage from './pages/NotificationsPage';
import ModerationPage from './pages/ModerationPage';
import ProfilePage from './pages/ProfilePage';
import { FAQPage, RulesPage } from './pages/StaticPages';
import CryptoStatusPage from './pages/CryptoStatusPage';

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Layout />}>
          <Route index element={<HomePage />} />
          <Route path="faq" element={<FAQPage />} />
          <Route path="rules" element={<RulesPage />} />
          <Route path="about" element={<AboutPage />} />
          <Route path="contact" element={<ContactPage />} />
          <Route path="notifications" element={<NotificationsPage />} />
          <Route path="moderation" element={<ModerationPage />} />
          <Route path="crypto" element={<CryptoStatusPage />} />
          <Route path="dm" element={<DirectMessagesPage />} />
          <Route path="dm/:hash" element={<DirectMessagesPage />} />
          <Route path="directory" element={<DirectoryPage />} />
          <Route path="u/:hash" element={<ProfilePage />} />
          <Route path=":board" element={<BoardPage />} />
          <Route path=":board/catalog" element={<CatalogPage />} />
          <Route path=":board/archive" element={<ArchivePage />} />
          <Route path=":board/thread/:id" element={<ThreadPage />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}
