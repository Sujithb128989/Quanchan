/*
 * Copyright (C) 2026 QuanChan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
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
import NotificationsPage from './pages/NotificationsPage';
import ModerationPage from './pages/ModerationPage';
import ProfilePage from './pages/ProfilePage';
import { FAQPage, RulesPage } from './pages/StaticPages';

export default function App() {
  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Layout />}>
          <Route index element={<HomePage />} />
          <Route path="faq" element={<FAQPage />} />
          <Route path="rules" element={<RulesPage />} />
          <Route path="about" element={<AboutPage />} />
          <Route path="notifications" element={<NotificationsPage />} />
          <Route path="moderation" element={<ModerationPage />} />
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
